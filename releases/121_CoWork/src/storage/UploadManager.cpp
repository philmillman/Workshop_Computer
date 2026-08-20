#include "UploadManager.h"
#include "Crc32.h"
#include "FlashOps.h"
#include "ConfigFormat.h"
#include "../SharedState.h"

#include <cstdio>
#include <cstring>
#include "hardware/flash.h"
#include "pico/stdlib.h"
#include "tusb.h"

namespace cowork {

// ---------- low-level I/O ----------

bool UploadManager::ReadExact(uint8_t *dst, uint32_t n)
{
	uint32_t got = 0;
	uint32_t lastRx = time_us_32();
	while (got < n) {
		if (pump_) pump_();
		uint32_t r = tud_cdc_read(dst + got, n - got);
		if (r > 0) {
			got += r;
			lastRx = time_us_32();
		} else if (time_us_32() - lastRx > kInterByteTimeoutUs) {
			return false;
		}
	}
	return true;
}

void UploadManager::WriteAll(const uint8_t *src, uint32_t n)
{
	uint32_t sent = 0;
	while (sent < n) {
		uint32_t w = tud_cdc_write(src + sent, n - sent);
		sent += w;
		tud_cdc_write_flush();
		if (pump_) pump_();
	}
}

void UploadManager::WriteStr(const char *s)
{
	WriteAll((const uint8_t *)s, (uint32_t)strlen(s));
}

void UploadManager::WriteU32(uint32_t v)
{
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	WriteAll(b, 4);
}

static uint32_t ReadU32(const uint8_t *b)
{
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// ---------- commands ----------

void UploadManager::Service()
{
	if (!tud_cdc_connected() && !tud_cdc_available()) return;
	if (!tud_cdc_available()) return;

	uint8_t cmd = 0;
	if (tud_cdc_read(&cmd, 1) != 1) return;

	switch (cmd) {
	case 'X': WriteStr("SYNC\n"); break;
	case 'Q': break; // stray abort between commands: ignore
	case 'I': CmdInfo(); break;
	case 'R': CmdRead(); break;
	case 'W': CmdWrite(); break;
	case 'E': CmdErase(); break;
	case 'G': CmdConfigGet(); break;
	case 'S': CmdConfigSet(); break;
	case 'T': CmdTransport(); break;
	default: break; // unknown byte: ignore (keeps framing recoverable via X)
	}
}

void UploadManager::CmdInfo()
{
	char json[512];
	int n = snprintf(json, sizeof(json),
		"{\"proto\":\"COWORK1\",\"fw\":\"0.1.0\",\"flash\":%lu,"
		"\"config\":{\"off\":%lu,\"len\":4096},"
		"\"songs\":{\"off\":%lu,\"slotLen\":%lu,\"slots\":%lu},"
		"\"samples\":{\"off\":%lu,\"dataOff\":%lu,\"end\":%lu},"
		"\"state\":{\"playing\":%s,\"role\":\"%s\",\"linked\":%s,"
		"\"mode\":%lu,\"song\":%lu,\"bpm\":%lu.%lu}}",
		(unsigned long)map_.flashBytes,
		(unsigned long)map_.configOff,
		(unsigned long)map_.songsOff, (unsigned long)map_.songSlotBytes,
		(unsigned long)map_.songSlots,
		(unsigned long)map_.bankOff, (unsigned long)map_.bankDataOff,
		(unsigned long)map_.bankEnd,
		gShared.transportRunning ? "true" : "false",
		gShared.statusRole ? "follower" : "leader",
		gShared.peerConnected ? "true" : "false",
		(unsigned long)gShared.statusMode, (unsigned long)gShared.statusSong,
		(unsigned long)(gShared.statusBpmX10 / 10),
		(unsigned long)(gShared.statusBpmX10 % 10));
	if (n < 0) n = 0;
	WriteU32((uint32_t)n);
	WriteAll((const uint8_t *)json, (uint32_t)n);
}

void UploadManager::CmdRead()
{
	uint8_t args[8];
	if (!ReadExact(args, sizeof(args))) return;
	uint32_t off = ReadU32(args);
	uint32_t len = ReadU32(args + 4);

	if (!map_.InWritableRegion(off, len)) {
		WriteU32(0);
		return;
	}
	WriteU32(len);

	gShared.uploadActive = 1;
	uint32_t crc = Crc32::Begin();
	const uint8_t *src = (const uint8_t *)(XIP_BASE + off);
	uint32_t sent = 0;
	bool aborted = false;
	while (sent < len) {
		uint32_t n = (len - sent) < kChunk ? (len - sent) : kChunk;
		WriteAll(src + sent, n);
		crc = Crc32::Update(crc, src + sent, n);
		sent += n;
		if (sent < len) {
			uint8_t ack = 0;
			if (!ReadExact(&ack, 1) || ack != 'A') { aborted = true; break; }
		}
	}
	gShared.uploadActive = 0;
	if (aborted) return;
	WriteU32(Crc32::Final(crc));
	WriteStr("DONE\n");
}

void UploadManager::CmdWrite()
{
	uint8_t args[12];
	if (!ReadExact(args, sizeof(args))) return;
	uint32_t off = ReadU32(args);
	uint32_t len = ReadU32(args + 4);
	uint32_t wantCrc = ReadU32(args + 8);

	if (gShared.transportRunning) { WriteStr("ERR BUSY\n"); return; }
	if (!map_.InWritableRegion(off, len)) { WriteStr("ERR RANGE\n"); return; }
	if (off & (kSectorSize - 1)) { WriteStr("ERR ALIGN\n"); return; }
	if (len == 0 || len > map_.flashBytes) { WriteStr("ERR LEN\n"); return; }

	if (!FlashOps::AcquireQuiesce()) { WriteStr("ERR BUSY\n"); return; }
	WriteStr("OK\n");
	gShared.uploadActive = 1;

	uint32_t crc = Crc32::Begin();
	uint32_t nextErase = off;
	uint32_t pos = 0;
	uint8_t page[kPageSize];
	bool timeout = false;

	while (pos < len) {
		uint32_t n = (len - pos) < kPageSize ? (len - pos) : kPageSize;
		if (!ReadExact(page, n)) { timeout = true; break; }
		crc = Crc32::Update(crc, page, n);
		if (n < kPageSize) memset(page + n, 0xFF, kPageSize - n);

		uint32_t dst = off + pos;
		uint32_t ints = save_and_disable_interrupts();
		if (dst >= nextErase) {
			flash_range_erase(nextErase, kSectorSize);
			nextErase += kSectorSize;
		}
		flash_range_program(dst, page, kPageSize);
		restore_interrupts(ints);
		pos += n;
	}

	bool ok = false;
	if (!timeout && Crc32::Final(crc) == wantCrc) {
		// Verify against read-back (still quiesced; XIP reads are safe)
		uint32_t rb = Crc32::Compute((const uint8_t *)(XIP_BASE + off), len);
		ok = (rb == wantCrc);
	}

	gShared.uploadActive = 0;
	FlashOps::ReleaseQuiesce();

	if (timeout) { WriteStr("TIMEOUT\n"); return; }
	if (!ok) { WriteStr("CRC\n"); return; }
	if (onMutated_) onMutated_();
	WriteStr("OK\n");
}

void UploadManager::CmdErase()
{
	uint8_t args[8];
	if (!ReadExact(args, sizeof(args))) return;
	uint32_t off = ReadU32(args);
	uint32_t len = ReadU32(args + 4);

	if (gShared.transportRunning) { WriteStr("ERR BUSY\n"); return; }
	if (!map_.InWritableRegion(off, len)) { WriteStr("ERR RANGE\n"); return; }
	if ((off & (kSectorSize - 1)) || (len & (kSectorSize - 1))) { WriteStr("ERR ALIGN\n"); return; }

	if (!FlashOps::AcquireQuiesce()) { WriteStr("ERR BUSY\n"); return; }
	gShared.uploadActive = 1;
	// Erase sector-by-sector, re-enabling interrupts between sectors so
	// core 1 stays responsive.
	for (uint32_t s = 0; s < len; s += kSectorSize) {
		uint32_t ints = save_and_disable_interrupts();
		flash_range_erase(off + s, kSectorSize);
		restore_interrupts(ints);
		if (pump_) pump_();
	}
	gShared.uploadActive = 0;
	FlashOps::ReleaseQuiesce();
	if (onMutated_) onMutated_();
	WriteStr("OK\n");
}

void UploadManager::CmdConfigGet()
{
	WriteU32(kConfigBytes);
	WriteAll((const uint8_t *)&gShared.configStaging, kConfigBytes);
}

void UploadManager::CmdConfigSet()
{
	uint8_t lenBuf[4];
	if (!ReadExact(lenBuf, 4)) return;
	uint32_t len = ReadU32(lenBuf);
	if (len != kConfigBytes || len > 4096) {
		// Consume and discard a bounded body so framing recovers
		uint8_t sink[64];
		uint32_t left = (len < 4096) ? len + 4 : 0;
		while (left) {
			uint32_t n = left < sizeof(sink) ? left : sizeof(sink);
			if (!ReadExact(sink, n)) break;
			left -= n;
		}
		WriteStr("ERR LEN\n");
		return;
	}

	Config c;
	uint8_t crcBuf[4];
	if (!ReadExact((uint8_t *)&c, kConfigBytes)) return;
	if (!ReadExact(crcBuf, 4)) return;
	if (ReadU32(crcBuf) != Crc32::Compute((const uint8_t *)&c, kConfigBytes)) {
		WriteStr("CRC\n");
		return;
	}
	if (c.magic != kConfigMagic || c.version != kConfigVersion) {
		WriteStr("ERR LEN\n");
		return;
	}

	ConfigSanitize(c, map_.songSlots);
	gShared.configStaging = c;
	gShared.configApplySeq = gShared.configApplySeq + 1;
	WriteStr("OK\n");
}

void UploadManager::CmdTransport()
{
	uint8_t sub = 0;
	if (!ReadExact(&sub, 1)) return;

	switch (sub) {
	case '?': {
		char json[512];
		int n = snprintf(json, sizeof(json),
			"{\"playing\":%s,\"role\":\"%s\",\"linked\":%s,"
			"\"mode\":%lu,\"song\":%lu,\"bpm\":%lu.%lu,\"tick\":%lu,"
			"\"diag\":{\"rs\":%lu,\"fw\":%lu,\"stp\":%lu,\"str\":%lu,"
			"\"gap\":%lu,\"drp\":%lu,\"ovr\":%lu},"
			"\"peerSeen\":%s,"
			"\"peer\":{\"rs\":%lu,\"fw\":%lu,\"stp\":%lu,\"str\":%lu,"
			"\"gap\":%lu,\"drp\":%lu,\"ovr\":%lu}}\n",
			gShared.transportRunning ? "true" : "false",
			gShared.statusRole ? "follower" : "leader",
			gShared.peerConnected ? "true" : "false",
			(unsigned long)gShared.statusMode, (unsigned long)gShared.statusSong,
			(unsigned long)(gShared.statusBpmX10 / 10),
			(unsigned long)(gShared.statusBpmX10 % 10),
			(unsigned long)gShared.statusTick,
			(unsigned long)gShared.diagResyncs,
			(unsigned long)gShared.diagFreewheels,
			(unsigned long)gShared.diagStopsRx,
			(unsigned long)gShared.diagStartsRx,
			(unsigned long)gShared.diagMaxGapMs,
			(unsigned long)gShared.diagMidiInDrops,
			(unsigned long)gShared.diagOverrun,
			gShared.peerDiagSeen ? "true" : "false",
			(unsigned long)gShared.peerDiag[0],
			(unsigned long)gShared.peerDiag[1],
			(unsigned long)gShared.peerDiag[2],
			(unsigned long)gShared.peerDiag[3],
			(unsigned long)(gShared.peerDiag[4] * 100), // sent as gap/100
			(unsigned long)gShared.peerDiag[5],
			(unsigned long)gShared.peerDiag[6]);
		if (n > 0) WriteAll((const uint8_t *)json, (uint32_t)n);
		break;
	}
	case 'S': {
		gShared.transportStopReq = 1;
		uint32_t start = time_us_32();
		while (gShared.transportRunning && time_us_32() - start < 100000)
			if (pump_) pump_();
		WriteStr("OK\n");
		break;
	}
	case 'P':
		if (gShared.statusRole != 0) { WriteStr("ERR ROLE\n"); break; }
		gShared.transportPlayReq = 1;
		WriteStr("OK\n");
		break;
	case 'R':
		if (onMutated_) onMutated_();
		WriteStr("OK\n");
		break;
	default:
		break;
	}
}

} // namespace cowork
