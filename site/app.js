// crosspoint-anki web flasher: page logic. Plain ES module, no build step.
import { CrossPointFlasher, imageChipName, validateFirmwareImage } from './flasher.js';

const REPO = 'H1D/crosspoint-anki';
const RELEASES_API = `https://api.github.com/repos/${REPO}/releases?per_page=30`;

// id = asset suffix in crosspoint-<tag>-<id>.bin (see .github/workflows/release.yml).
// bootSteps: what the user does after a successful flash, shown in the done dialog.
// resetAfterFlash: pulse EN over RTS when done instead of leaving the chip in the
// esptool stub. The Sticky needs it: its power button is a plain GPIO that can't
// reset the chip and its battery keeps it powered when USB is unplugged, so a
// device left in the stub stays dark. After the reset it boots, sees USB power
// and goes to deep sleep, which the power button can wake it from.
const DEVICES = [
  {
    id: 'x3-x4', name: 'Xteink X3 / X4', chip: 'ESP32-C3',
    bootSteps: [
      'Unplug the USB cable and plug it back in.',
      'Press and hold the power button for 3-5 seconds, then let go. Do not press Reset.',
    ],
  },
  {
    id: 'x4pro', name: 'Xteink X4 Pro', chip: 'ESP32-S3',
    bootSteps: ['Unplug the USB cable and plug it back in.', 'Press and hold the power button to boot.'],
  },
  {
    id: 'x4c', name: 'Xteink X4 Classic (X4C)', chip: 'ESP32-S3',
    bootSteps: ['Unplug the USB cable and plug it back in.', 'Press and hold the power button to boot.'],
  },
  {
    id: 'sticky', name: 'Seeed reTerminal Sticky', chip: 'ESP32-S3',
    resetAfterFlash: true,
    bootSteps: [
      'The flasher has restarted the Sticky and it is now asleep.',
      'Press and hold the power button (top right) for about 3 seconds, then let go.',
    ],
    firstInstallNote: true,
  },
  {
    id: 'papermono', name: 'M5 PaperMono', chip: 'ESP32-S3',
    bootSteps: ['Press the power button to boot the device.'],
    firstInstallNote: true,
  },
];

const $ = (id) => document.getElementById(id);
const ui = {
  browserNotice: $('browser-notice'),
  device: $('device'),
  release: $('release'),
  refresh: $('refresh'),
  releaseInfo: $('release-info'),
  file: $('file'),
  fileInfo: $('file-info'),
  flash: $('flash'),
  steps: $('steps'),
  progressLabel: $('progress-label'),
  progress: $('progress'),
  log: $('log'),
  result: $('result'),
  firstInstall: $('first-install-note'),
  doneDialog: $('done-dialog'),
  doneSummary: $('done-summary'),
  doneSteps: $('done-steps'),
};

let releases = [];
let localFile = null;
let busy = false;

// --- helpers ---

function log(line) {
  ui.log.textContent += line + '\n';
  ui.log.scrollTop = ui.log.scrollHeight;
}

const terminal = {
  clean() {},
  writeLine(s) { log(String(s)); },
  write(s) { ui.log.textContent += String(s); ui.log.scrollTop = ui.log.scrollHeight; },
};

function setResult(kind, html) {
  ui.result.className = `result ${kind}`;
  ui.result.innerHTML = html;
  ui.result.hidden = false;
}

function formatSize(n) {
  return n >= 0x100000 ? (n / 0x100000).toFixed(2) + ' MB' : Math.round(n / 1024) + ' KB';
}

function currentDevice() {
  return DEVICES.find((d) => d.id === ui.device.value) || DEVICES[0];
}

function assetFor(release, device) {
  const name = `crosspoint-${release.tag_name}-${device.id}.bin`;
  return release.assets.find((a) => a.name === name) || null;
}

function setBusy(state) {
  busy = state;
  ui.flash.disabled = state;
  ui.device.disabled = state;
  ui.release.disabled = state;
  ui.file.disabled = state;
  ui.refresh.disabled = state;
}

// --- releases ---

async function loadReleases() {
  ui.release.innerHTML = '<option>Loading releases...</option>';
  ui.releaseInfo.textContent = '';
  releases = [];
  try {
    const res = await fetch(RELEASES_API, { headers: { Accept: 'application/vnd.github+json' } });
    if (!res.ok) throw new Error(`GitHub API returned ${res.status}${res.status === 403 ? ' (rate limited; try again later)' : ''}`);
    const list = await res.json();
    releases = list.filter((r) => !r.draft);
  } catch (err) {
    ui.release.innerHTML = '<option value="">Could not load releases</option>';
    ui.releaseInfo.textContent = `${err.message}. You can still flash a local .bin below.`;
    return;
  }
  if (releases.length === 0) {
    ui.release.innerHTML = '<option value="">No releases published yet</option>';
    ui.releaseInfo.textContent = 'Download a build from the repository, or flash a local .bin below.';
    return;
  }
  // The API returns newest first; preselect the newest release that carries
  // firmware assets (a freshly published release has none until CI finishes).
  ui.release.innerHTML = '';
  let preselect = null;
  for (const r of releases) {
    const opt = document.createElement('option');
    opt.value = r.tag_name;
    opt.textContent = r.tag_name + (r.prerelease ? ' (pre-release)' : '');
    ui.release.appendChild(opt);
    if (preselect === null && r.assets.some((a) => /^crosspoint-.*\.bin$/.test(a.name))) preselect = r.tag_name;
  }
  ui.release.value = preselect || releases[0].tag_name;
  updateReleaseInfo();
}

function updateReleaseInfo() {
  const release = releases.find((r) => r.tag_name === ui.release.value);
  const device = currentDevice();
  ui.firstInstall.hidden = !device.firstInstallNote;
  if (!release) { ui.releaseInfo.textContent = ''; return; }
  const asset = assetFor(release, device);
  const when = release.published_at ? new Date(release.published_at).toLocaleDateString() : '';
  if (asset) {
    ui.releaseInfo.innerHTML =
      `<a href="${release.html_url}" target="_blank" rel="noopener">${release.tag_name}</a> (${when}): ` +
      `<a href="${asset.browser_download_url}">${asset.name}</a>, ${formatSize(asset.size)}`;
  } else {
    ui.releaseInfo.innerHTML =
      `<a href="${release.html_url}" target="_blank" rel="noopener">${release.tag_name}</a> has no ` +
      `<code>crosspoint-${release.tag_name}-${device.id}.bin</code> asset` +
      (release.assets.length === 0 ? ' yet (the release build may still be running).' : '.');
  }
}

// GitHub release assets redirect to release-assets.githubusercontent.com,
// which sends no Access-Control-Allow-Origin header, so a browser can't
// fetch them directly. pages.yml mirrors the assets of recent releases into
// firmware/<tag>/ on this site; try that first, then the direct URL (in case
// GitHub ever allows it), and finally ask for a manual download.
async function fetchReleaseFirmware(release, asset) {
  const mirror = `firmware/${encodeURIComponent(release.tag_name)}/${encodeURIComponent(asset.name)}`;
  try {
    const res = await fetch(mirror, { cache: 'no-cache' });
    if (res.ok) {
      log(`Downloaded ${asset.name} from the site mirror.`);
      return new Uint8Array(await res.arrayBuffer());
    }
    log(`No mirror copy of ${asset.name} (HTTP ${res.status}); trying GitHub directly...`);
  } catch (err) {
    log(`Mirror fetch failed: ${err.message}; trying GitHub directly...`);
  }
  try {
    const res = await fetch(asset.browser_download_url);
    if (res.ok) {
      log(`Downloaded ${asset.name} from GitHub.`);
      return new Uint8Array(await res.arrayBuffer());
    }
    throw new Error(`HTTP ${res.status}`);
  } catch (err) {
    throw new Error(
      `Could not download ${asset.name} in the browser (${err.message}). ` +
      `<a href="${asset.browser_download_url}">Download it manually</a> and pick it under "Flash a local .bin".`
    );
  }
}

// --- flashing ---

function showDoneDialog(device, sourceLabel, partition) {
  ui.doneSummary.textContent = `${sourceLabel} was written to ${partition}.`;
  ui.doneSteps.innerHTML = '';
  for (const text of device.bootSteps) {
    const li = document.createElement('li');
    li.textContent = text;
    ui.doneSteps.appendChild(li);
  }
  ui.doneDialog.showModal();
}

function renderSteps(names, states) {
  ui.steps.innerHTML = '';
  names.forEach((name, i) => {
    const li = document.createElement('li');
    li.className = states[i] || 'pending';
    li.textContent = name;
    ui.steps.appendChild(li);
  });
}

async function flash() {
  if (busy) return;
  const device = currentDevice();
  ui.result.hidden = true;
  ui.log.textContent = '';
  ui.progress.value = 0;
  ui.progressLabel.textContent = '';
  ui.steps.innerHTML = '';

  if (!('serial' in navigator && navigator.serial)) {
    setResult('error', 'WebSerial is not available. Use Chrome or Edge on a desktop computer.');
    return;
  }

  // requestPort() must be called inside the click gesture, before any await.
  let port;
  try {
    port = await CrossPointFlasher.requestPort();
  } catch (err) {
    if (err.name !== 'NotFoundError') setResult('error', `Serial port: ${err.message}`);
    return;
  }

  setBusy(true);
  const stepNames = ['Get firmware', 'Connect to device', 'Read partition table', 'Read OTA data', 'Write firmware',
    'Update boot partition', device.resetAfterFlash ? 'Reset device' : 'Disconnect'];
  const states = [];
  const paint = () => renderSteps(stepNames, states);
  try {
    states[0] = 'running'; paint();
    let firmware, sourceLabel;
    if (localFile) {
      firmware = new Uint8Array(await localFile.arrayBuffer());
      sourceLabel = localFile.name;
    } else {
      const release = releases.find((r) => r.tag_name === ui.release.value);
      if (!release) throw new Error('No release selected. Pick a release or a local .bin file.');
      const asset = assetFor(release, device);
      if (!asset) throw new Error(`Release ${release.tag_name} has no firmware for ${device.name}.`);
      firmware = await fetchReleaseFirmware(release, asset);
      sourceLabel = asset.name;
    }
    log(`Firmware: ${sourceLabel} (${firmware.length} bytes)`);
    await validateFirmwareImage(firmware);
    const imgChip = imageChipName(firmware);
    if (imgChip && imgChip !== device.chip) {
      throw new Error(`${sourceLabel} is built for the ${imgChip}, but the ${device.name} uses an ${device.chip}. Nothing was written.`);
    }
    states[0] = 'done'; paint();

    const flasher = new CrossPointFlasher(port, { expectedChip: device.chip, deviceName: device.name, terminal });
    const result = await flasher.flashFirmware(firmware, {
      skipReset: !device.resetAfterFlash,
      onLog: log,
      onStepChange: (idx, _name, status) => { states[idx + 1] = status; paint(); },
      onProgress: (label, written, total) => {
        ui.progressLabel.textContent = `${label}: ${Math.round((written / total) * 100)}%`;
        ui.progress.max = total;
        ui.progress.value = written;
      },
    });
    ui.progressLabel.textContent = 'Done';
    setResult('ok',
      `<strong>Flashed ${sourceLabel} to ${result.partition}.</strong><br>${device.bootSteps.join(' ')}` +
      '<br><strong>The first boot is slow: wait at least 2 minutes before you touch anything.</strong>');
    showDoneDialog(device, sourceLabel, result.partition);
  } catch (err) {
    const idx = states.findIndex((s) => s === 'running');
    if (idx >= 0) states[idx] = 'error';
    paint();
    log(`ERROR: ${err.message}`);
    setResult('error', err.message);
    console.error(err);
  } finally {
    try { await port.close(); } catch {}
    setBusy(false);
  }
}

// --- wiring ---

if (!('serial' in navigator && navigator.serial)) {
  ui.browserNotice.hidden = false;
  ui.flash.disabled = true;
}

for (const d of DEVICES) {
  const opt = document.createElement('option');
  opt.value = d.id;
  opt.textContent = d.name;
  ui.device.appendChild(opt);
}

ui.device.addEventListener('change', updateReleaseInfo);
ui.release.addEventListener('change', updateReleaseInfo);
ui.refresh.addEventListener('click', loadReleases);
ui.file.addEventListener('change', () => {
  localFile = ui.file.files[0] || null;
  ui.fileInfo.textContent = localFile
    ? `${localFile.name} (${formatSize(localFile.size)}) will be flashed instead of the selected release.`
    : '';
  ui.flash.textContent = localFile ? 'Connect & Flash local .bin' : 'Connect & Flash';
});
$('clear-file').addEventListener('click', () => {
  ui.file.value = '';
  ui.file.dispatchEvent(new Event('change'));
});
ui.flash.addEventListener('click', flash);

updateReleaseInfo();
loadReleases();
