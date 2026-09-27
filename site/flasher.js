/**
 * crosspoint-anki web flasher: device side.
 *
 * Ported from crosspoint-tools (https://github.com/crosspoint-reader/crosspoint-tools,
 * src/lib/flasher.js, MIT License, Copyright (c) 2025 SoFriendly). Only the
 * OTA-slot flow is kept: read the partition table at 0x8000, write the app
 * image into the inactive OTA slot, then stamp a new otadata entry so the
 * bootloader picks that slot. The bootloader, partition table and NVS are
 * never touched.
 *
 * esptool-js is loaded from a CDN as an ES module. crosspoint-tools ships a
 * vendored esbuild bundle of esptool-js; its readFlash()/after() bodies are
 * identical to the published 0.5.7 package, so the CDN build is used here.
 */

const ESPTOOL_SOURCES = [
  'https://cdn.jsdelivr.net/npm/esptool-js@0.5.7/bundle.js',
  'https://unpkg.com/esptool-js@0.5.7/bundle.js',
];

let ESPLoader, Transport;

export async function loadEsptool() {
  if (ESPLoader) return;
  let lastErr;
  for (const url of ESPTOOL_SOURCES) {
    try {
      const mod = await import(url);
      ESPLoader = mod.ESPLoader;
      Transport = mod.Transport;
      if (ESPLoader && Transport) return;
    } catch (err) {
      lastErr = err;
    }
  }
  throw new Error(`Could not load esptool-js from a CDN: ${lastErr?.message || 'unknown error'}`);
}

// --- CRC32 (IDF crc32_le) ---

const CRC32_TABLE = new Uint32Array(256);
for (let i = 0; i < 256; i++) {
  let crc = i;
  for (let j = 0; j < 8; j++) crc = (crc & 1) ? (0xEDB88320 ^ (crc >>> 1)) : (crc >>> 1);
  CRC32_TABLE[i] = crc >>> 0;
}

function crc32(data, previous = 0) {
  let crc = previous === 0 ? 0 : (previous ^ 0xFFFFFFFF) >>> 0;
  for (let i = 0; i < data.length; i++) crc = CRC32_TABLE[(crc ^ data[i]) & 0xFF] ^ (crc >>> 8);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

// --- Byte utilities ---

function u32ToLeBytes(val) {
  return new Uint8Array([val & 0xFF, (val >>> 8) & 0xFF, (val >>> 16) & 0xFF, (val >>> 24) & 0xFF]);
}

function leBytesToU32(bytes) {
  return ((bytes[0] || 0) + (((bytes[1] || 0) << 8) >>> 0) +
    (((bytes[2] || 0) << 16) >>> 0) + (((bytes[3] || 0) << 24) >>> 0)) >>> 0;
}

function isEqualBytes(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

// ESP-IDF stores crc32_le(UINT32_MAX, &ota_seq, 4) in each otadata entry.
function generateCrc32Le(sequence) {
  return u32ToLeBytes(crc32(u32ToLeBytes(sequence), 0xFFFFFFFF));
}

// --- Firmware image validation ---

const ESP_IMAGE_MAGIC = 0xE9;
const IMG_HEADER_SIZE = 24;
const IMG_SEG_HEADER_SIZE = 8;
const IMG_SHA_TRAILER = 32;
const IMG_CHECKSUM_SEED = 0xEF;
const IMG_HASH_APPENDED_OFFSET = 23;

// esp_chip_id_t from the ESP image extended header (bytes 12-13, LE).
const ESP_IMAGE_CHIP_IDS = {
  'ESP32': 0x0000,
  'ESP32-S2': 0x0002,
  'ESP32-C3': 0x0005,
  'ESP32-S3': 0x0009,
};

export function imageChipName(data) {
  if (data.length < 14) return null;
  const id = data[12] | (data[13] << 8);
  const entry = Object.entries(ESP_IMAGE_CHIP_IDS).find(([, chipId]) => chipId === id);
  return entry ? entry[0] : null;
}

// Walks the ESP image: 24-byte header, segments (8-byte header + data),
// padding to 16, XOR checksum byte, optional SHA-256 trailer. Rejects HTML
// error pages, truncated downloads and wrong-shape files before anything is
// written. Bounds use headroom-first arithmetic so a hostile dataLen can't
// wrap the addition.
export async function validateFirmwareImage(data) {
  const totalSize = data.length;
  if (totalSize < IMG_HEADER_SIZE) throw new Error('Firmware too small: header is truncated.');
  if (data[0] !== ESP_IMAGE_MAGIC) {
    throw new Error('Invalid firmware: ESP image magic byte (0xE9) missing. Is this really a firmware .bin?');
  }
  const segCount = data[1];
  const hashAppended = (data[IMG_HASH_APPENDED_OFFSET] & 0x01) !== 0;

  let xorAccum = IMG_CHECKSUM_SEED;
  let pos = IMG_HEADER_SIZE;
  for (let i = 0; i < segCount; i++) {
    if (totalSize - pos < IMG_SEG_HEADER_SIZE) throw new Error('Invalid firmware: segment header runs past end of file.');
    const dataLen = leBytesToU32(data.subarray(pos + 4, pos + 8));
    pos += IMG_SEG_HEADER_SIZE;
    if (dataLen > totalSize - pos) throw new Error('Invalid firmware: segment data runs past end of file.');
    const end = pos + dataLen;
    for (let j = pos; j < end; j++) xorAccum ^= data[j];
    pos = end;
  }

  const padEnd = (pos + 16) & ~15;
  const expectedTotal = padEnd + (hashAppended ? IMG_SHA_TRAILER : 0);
  if (expectedTotal !== totalSize) {
    throw new Error(`Invalid firmware: declared size ${expectedTotal} does not match file size ${totalSize}.`);
  }
  const storedChecksum = data[padEnd - 1];
  if ((xorAccum & 0xFF) !== storedChecksum) {
    throw new Error(`Invalid firmware: segment checksum mismatch (computed 0x${(xorAccum & 0xFF).toString(16)}, stored 0x${storedChecksum.toString(16)}).`);
  }
  if (hashAppended) {
    const body = data.subarray(0, totalSize - IMG_SHA_TRAILER);
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', body));
    const stored = data.subarray(totalSize - IMG_SHA_TRAILER);
    if (!isEqualBytes(digest, stored)) throw new Error('Invalid firmware: SHA-256 trailer mismatch. File is corrupt or truncated.');
  }
}

// --- otadata ---
//
// IDF otadata format: two 4 KB flash sectors, each holding one
// esp_ota_select_entry_t at byte 0 (ota_seq u32 @0, ota_state u32 @0x18,
// crc u32 @0x1C). A protocol constant, so not read from the partition table.

const OTA_SECTOR_BYTES = 0x1000;
const OTADATA_BYTES = 2 * OTA_SECTOR_BYTES;

const OTA_STATE = { NEW: 0, PENDING_VERIFY: 1, VALID: 2, INVALID: 3, ABORTED: 4, UNDEFINED: 0xFFFFFFFF };
const INVALID_STATES = new Set([OTA_STATE.INVALID, OTA_STATE.ABORTED]);

function parseOtaPartitionSlot(data, offset) {
  const sequence = leBytesToU32(data.slice(offset, offset + 4));
  const state = leBytesToU32(data.slice(offset + 0x18, offset + 0x1C));
  const crcBytes = data.slice(offset + 0x1C, offset + 0x20);
  return { sequence, state, crcValid: isEqualBytes(crcBytes, generateCrc32Le(sequence)) };
}

// IDF OTA model: active app slot = (active_seq - 1) % 2. The sector holding
// the active entry has no fixed relation to the app slot it boots; the new
// entry always goes into the OTHER sector.
export function parseOtadata(data) {
  const slot0 = parseOtaPartitionSlot(data, 0);
  const slot1 = parseOtaPartitionSlot(data, OTA_SECTOR_BYTES);

  const eligible = [];
  if (slot0.sequence !== 0xFFFFFFFF && slot0.crcValid && !INVALID_STATES.has(slot0.state)) eligible.push({ sector: 0, seq: slot0.sequence });
  if (slot1.sequence !== 0xFFFFFFFF && slot1.crcValid && !INVALID_STATES.has(slot1.state)) eligible.push({ sector: 1, seq: slot1.sequence });
  eligible.sort((a, b) => b.seq - a.seq);

  let activeSector, activeSeq, activeApp;
  if (eligible.length === 0) {
    // Erased otadata: the bootloader defaults to app0.
    activeSector = -1;
    activeSeq = 0;
    activeApp = 0;
  } else {
    activeSector = eligible[0].sector;
    activeSeq = eligible[0].seq;
    activeApp = (activeSeq - 1) % 2;
  }
  const inactiveApp = 1 - activeApp;
  // Smallest seq > activeSeq that maps to inactiveApp. With an active entry
  // that's activeSeq + 1; for erased otadata it steps to 2 so app1 is picked.
  let newSeq = activeSeq + 1;
  while (((newSeq - 1) % 2) !== inactiveApp) newSeq++;
  const targetSector = activeSector < 0 ? 0 : (1 - activeSector);

  return { slot0, slot1, activeApp, inactiveApp, activeSeq, newSeq, targetSector };
}

// Builds the 4 KB target sector with the new entry at byte 0; the other
// sector stays on flash as a fallback boot pointer if power drops mid-write.
// State is NEW (not VALID) so rollback-enabled bootloaders (X4 Pro) can fall
// back to the previous slot when the new image doesn't boot.
function buildNewOtadataSector(existingSectorData, newSeq) {
  const newData = new Uint8Array(existingSectorData);
  newData.set(u32ToLeBytes(newSeq), 0);
  newData.set(u32ToLeBytes(OTA_STATE.NEW), 0x18);
  newData.set(generateCrc32Le(newSeq), 0x1C);
  return newData;
}

function assertOtadataSwitch(ota, expectedApp, expectedSeq) {
  if (ota.activeSeq !== expectedSeq || ota.activeApp !== expectedApp) {
    throw new Error(
      `OTA boot selector did not verify after write. Expected app${expectedApp} via seq ${expectedSeq}, ` +
      `got app${ota.activeApp} via seq ${ota.activeSeq} ` +
      `(slot0 seq ${ota.slot0.sequence} crc ${ota.slot0.crcValid ? 'ok' : 'bad'}, ` +
      `slot1 seq ${ota.slot1.sequence} crc ${ota.slot1.crcValid ? 'ok' : 'bad'}).`
    );
  }
}

// --- Partition table ---

const PARTITION_TYPES = {
  0x00: { 0x10: 'app-ota_0', 0x11: 'app-ota_1' },
  0x01: { 0x00: 'data-ota', 0x01: 'data-phy', 0x02: 'data-nvs', 0x03: 'data-coredump', 0x82: 'data-spiffs' },
};

export function parsePartitionTable(data) {
  const partitions = [];
  for (let offset = 0; offset < data.length; offset += 32) {
    const chunk = data.slice(offset, offset + 32);
    if (chunk.length !== 32) break;
    let allFF = true;
    for (let i = 0; i < 32; i++) { if (chunk[i] !== 0xFF) { allFF = false; break; } }
    if (allFF) break;
    if (chunk[0] === 0xEB && chunk[1] === 0xEB) continue;  // MD5 checksum row
    const type = PARTITION_TYPES[chunk[2]]?.[chunk[3]] || 'unknown';
    const off = leBytesToU32(chunk.slice(4, 8));
    const size = leBytesToU32(chunk.slice(8, 12));
    let label = '';
    for (let i = 12; i < 28 && chunk[i] !== 0; i++) label += String.fromCharCode(chunk[i]);
    partitions.push({ type, offset: off, size, label });
  }
  return partitions;
}

// 0x0..0x8000 holds the 2nd-stage bootloader and 0x8000..0x9000 the table
// itself; any slot below 0x9000, wrapping uint32 or past 16 MB is rejected
// before any erase.
function extractLayout(partitions) {
  let otadata = null, app0 = null, app1 = null;
  for (const p of partitions) {
    if (p.type === 'data-ota') otadata = p;
    else if (p.type === 'app-ota_0') app0 = p;
    else if (p.type === 'app-ota_1') app1 = p;
  }
  if (!otadata) throw new Error('Partition table has no otadata partition.');
  if (!app0 || !app1) throw new Error('Partition table is missing an OTA app slot.');
  if (otadata.size < OTADATA_BYTES) throw new Error(`Partition table otadata is too small: ${otadata.size} bytes (need ${OTADATA_BYTES}).`);
  for (const p of [otadata, app0, app1]) {
    const end = (p.offset + p.size) >>> 0;
    if (p.offset < 0x9000 || end < p.offset || end > 0x1000000) {
      throw new Error(`Partition ${p.type} range 0x${p.offset.toString(16)}..0x${end.toString(16)} is outside the safe flash window.`);
    }
  }
  return {
    otadataOffset: otadata.offset,
    appSlots: [
      { offset: app0.offset, size: app0.size },
      { offset: app1.offset, size: app1.size },
    ],
  };
}

// --- Flasher ---

// Vendor-level WebSerial filters: Espressif (USB-Serial-JTAG, ROM download
// mode, TinyUSB CDC), Seeed (stock Sticky firmware), Silicon Labs and WCH
// USB-UART bridges. No filter at all would also list Bluetooth serial ports.
export const PORT_FILTERS = [
  { usbVendorId: 0x303a },
  { usbVendorId: 0x2886 },
  { usbVendorId: 0x10c4 },
  { usbVendorId: 0x1a86 },
];

export class CrossPointFlasher {
  // expectedChip: esptool CHIP_NAME the device must report ('ESP32-C3',
  // 'ESP32-S3'); connect() aborts on a mismatch before anything is written.
  // terminal: optional { clean(), writeLine(s), write(s) } for esptool output.
  constructor(port, { expectedChip = null, deviceName = null, terminal = null } = {}) {
    this.port = port;
    this.expectedChip = expectedChip;
    this.deviceName = deviceName;
    this.terminal = terminal;
    this.espLoader = null;
    this.layout = null;
  }

  // Must run synchronously inside a user gesture (click handler), before any await.
  static async requestPort(filters = PORT_FILTERS) {
    if (!('serial' in navigator && navigator.serial)) {
      throw new Error('WebSerial is not supported in this browser. Use Chrome or Edge.');
    }
    return navigator.serial.requestPort(filters ? { filters } : {});
  }

  async connect() {
    await loadEsptool();
    const transport = new Transport(this.port, false);
    // 115200 is the ROM baud; native USB (USB-Serial-JTAG / CDC) ignores it
    // and the stub switch-up is a no-op there.
    this.espLoader = new ESPLoader({
      transport, baudrate: 115200, romBaudrate: 115200, enableTracing: false,
      terminal: this.terminal || undefined,
    });
    await this.espLoader.main();

    const chipName = this.espLoader.chip?.CHIP_NAME;
    if (this.expectedChip && chipName && chipName !== this.expectedChip) {
      const label = this.deviceName ? `the ${this.deviceName}` : 'the selected device';
      try { await this.disconnect(true); } catch {}
      throw new Error(
        `Connected device is an ${chipName}, but ${label} uses an ${this.expectedChip}. ` +
        'Wrong device selected? Nothing was written.'
      );
    }
    return chipName;
  }

  async disconnect(skipReset = false) {
    if (!this.espLoader) return;
    // Keep GPIO0 high (DTR released), then pulse EN via RTS. esptool-js's
    // HardReset only releases RTS, so assert it first for a real edge.
    try { await this.espLoader.transport.setDTR(false); } catch {}
    if (skipReset) {
      await this.espLoader.after('no_reset_stub');
    } else {
      await this.espLoader.transport.setRTS(true);
      await new Promise((resolve) => setTimeout(resolve, 100));
      await this.espLoader.after('hard_reset');
    }
    // Clear both lines before close so the OS releasing the port doesn't
    // glitch EN/IO0 into another download-mode reset.
    try {
      await this.espLoader.transport.setDTR(false);
      await this.espLoader.transport.setRTS(false);
      await new Promise((resolve) => setTimeout(resolve, 100));
    } catch {}
    await this.espLoader.transport.disconnect();
    this.espLoader = null;
  }

  async readLayout() {
    // The table is one 4 KB sector at 0x8000; parsing stops at the first
    // all-0xFF entry.
    const data = await this.espLoader.readFlash(0x8000, 0x1000);
    const partitions = parsePartitionTable(data);
    this.layout = extractLayout(partitions);
    return partitions;
  }

  // Writes firmwareData into the inactive OTA slot and points otadata at it.
  // skipReset=true leaves the device in the stub (the user power-cycles it),
  // which is what the upstream flasher does for CrossPoint images: the new
  // app validates itself on first boot before committing the OTA pointer.
  async flashFirmware(firmwareData, { onStepChange, onProgress, onLog, skipReset = true } = {}) {
    const steps = [
      'Connect to device',
      'Read partition table',
      'Read OTA data',
      'Write firmware',
      'Update boot partition',
      skipReset ? 'Disconnect' : 'Reset device',
    ];
    const step = (idx, status) => { if (onStepChange) onStepChange(idx, steps[idx], status); };
    const log = (msg) => { if (onLog) onLog(msg); };

    await validateFirmwareImage(firmwareData);

    step(0, 'running');
    const chip = await this.connect();
    log(`Connected: ${chip}`);
    step(0, 'done');

    step(1, 'running');
    const partitions = await this.readLayout();
    for (const p of partitions) {
      log(`  ${(p.label || p.type).padEnd(10)} ${p.type.padEnd(14)} 0x${p.offset.toString(16).padStart(6, '0')}  ${p.size} bytes`);
    }
    step(1, 'done');

    step(2, 'running');
    const otaRaw = await this.espLoader.readFlash(this.layout.otadataOffset, OTADATA_BYTES, (_, p, t) => {
      if (onProgress) onProgress('Read OTA data', p, t);
    });
    const ota = parseOtadata(otaRaw);
    log(`otadata: running app${ota.activeApp} (seq ${ota.activeSeq}), writing app${ota.inactiveApp} (seq ${ota.newSeq}, sector ${ota.targetSector})`);
    step(2, 'done');

    step(3, 'running');
    const destSlot = this.layout.appSlots[ota.inactiveApp];
    if (firmwareData.length > destSlot.size) {
      throw new Error(`Firmware too large: ${firmwareData.length} bytes won't fit in app${ota.inactiveApp} (${destSlot.size} bytes).`);
    }
    log(`Writing ${firmwareData.length} bytes to app${ota.inactiveApp} at 0x${destSlot.offset.toString(16)}`);
    await this.espLoader.writeFlash({
      fileArray: [{ data: this.espLoader.ui8ToBstr(firmwareData), address: destSlot.offset }],
      flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep',
      eraseAll: false, compress: true,
      reportProgress: (_, written, total) => { if (onProgress) onProgress('Write firmware', written, total); },
    });
    step(3, 'done');

    step(4, 'running');
    const sectorStart = ota.targetSector * OTA_SECTOR_BYTES;
    const existingSector = otaRaw.subarray(sectorStart, sectorStart + OTA_SECTOR_BYTES);
    const newSector = buildNewOtadataSector(existingSector, ota.newSeq);
    await this.espLoader.writeFlash({
      fileArray: [{ data: this.espLoader.ui8ToBstr(newSector), address: this.layout.otadataOffset + sectorStart }],
      flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep',
      eraseAll: false, compress: true,
      reportProgress: (_, written, total) => { if (onProgress) onProgress('Update boot partition', written, total); },
    });
    const verify = parseOtadata(await this.espLoader.readFlash(this.layout.otadataOffset, OTADATA_BYTES));
    assertOtadataSwitch(verify, ota.inactiveApp, ota.newSeq);
    step(4, 'done');

    step(5, 'running');
    await this.disconnect(skipReset);
    step(5, 'done');

    return { partition: `app${ota.inactiveApp}`, offset: destSlot.offset };
  }
}
