import { AppletType, HidNpadButton } from '@nx.js/constants';
import { installNsp } from '@nx.js/install-title';
import { NcmStorageId } from '@nx.js/ncm';
import { NACP } from '@tootallnate/nacp';
import { paintInstaller } from './installer_canvas';
import { prepareVisibleInstaller } from './preparation';
import type { GameContext } from './installer_view';
import type { Module } from './hacbrewpack';
import { loadForwarderKeys } from './prod-keys';
import { deterministicTitleId } from './util';

const requestPath = 'sdmc:/switch/SwitchNOW/forwarder_request.json';
const resultPath = 'sdmc:/switch/SwitchNOW/forwarder_result.json';
let exitEnabled = false;
let plusReleasedAfterInstall = false;

function waitForPaint(): Promise<void> {
  return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}

const debugPath = 'sdmc:/switch/SwitchNOW/forwarder_debug.log';
let debugLines = '';
function trace(event: string) {
  debugLines = (debugLines + `${new Date().toISOString()} FORWARDER ${event}\n`).slice(-24000);
  try { Switch.writeFileSync(debugPath, debugLines); } catch { /* No SD logging must block install. */ }
}
trace('installer_start R63');
let context: GameContext = {};
try {
  const bytes = Switch.readFileSync('sdmc:/switch/SwitchNOW/.shortcut/game_context.json');
  context = bytes ? JSON.parse(new TextDecoder().decode(bytes)) : {};
  trace('context_loaded');
} catch { trace('context_unavailable'); }
let stage = 0;
let failed = false;
let background: ImageBitmap | undefined;
let painted = false;
let backgroundPainted = false;
let reportedFrameError = false;
const canvas = screen.getContext('2d');
if (!canvas) { trace('canvas_unavailable'); throw new Error('Installer canvas unavailable'); }
async function loadBackground() {
  if (!context.preview_background) return;
  trace('background_read');
  try {
    const bytes = Switch.readFileSync(context.preview_background);
    if (!bytes || bytes.byteLength > 12 * 1024 * 1024) throw new Error('background size');
    background = await createImageBitmap(new Blob([bytes], { type: 'image/png' }));
    trace(`background_ready ${background.width}x${background.height}`);
  } catch { trace('background_unavailable'); }
}
function keepInstallerAlive() {
  try {
    paintInstaller(canvas!, screen.width, screen.height, { language: context.language, stage, failed }, background);
    if (!painted) { painted = true; trace('first_frame_painted'); }
    if (background && !backgroundPainted) { backgroundPainted = true; trace('background_frame_painted'); }
    const rawButtons = navigator.getGamepads()[0]?.rawButtons ?? 0n;
    const plusPressed = (rawButtons & BigInt(HidNpadButton.Plus)) !== 0n;
    if (!exitEnabled) plusReleasedAfterInstall = false;
    else {
      if (!plusPressed) plusReleasedAfterInstall = true;
      if (plusReleasedAfterInstall && plusPressed) Switch.exit();
    }
  } catch {
    if (!reportedFrameError) { reportedFrameError = true; trace('paint_or_input_failed'); }
  }
  requestAnimationFrame(keepInstallerAlive);
}
requestAnimationFrame(keepInstallerAlive);

interface InstallRequest {
  version: 1;
  name: string;
  author: string;
  displayVersion: string;
  nroPath: string;
  returnPath: string;
  args: string;
  automatic?: boolean;
}

function readRequest(): InstallRequest {
  const data = Switch.readFileSync(requestPath);
  if (!data) throw new Error('SwitchNOW forwarder request is missing.');
  const request = JSON.parse(new TextDecoder().decode(data)) as InstallRequest;
  if (
    request.version !== 1 ||
    !request.name ||
    !request.nroPath.startsWith('sdmc:/') ||
    !request.returnPath?.startsWith('sdmc:/') ||
    !request.returnPath.endsWith('.nro') ||
    !request.args.includes(request.nroPath) ||
    request.name.length > 511 ||
    request.args.length > 1800
  ) {
    throw new Error('SwitchNOW forwarder request is invalid.');
  }
  return request;
}

async function returnToSwitchNow(request: InstallRequest): Promise<never> {
  trace('return_requested');
  await waitForPaint();
  await waitForPaint();
  return new Switch.Application(request.returnPath).launch(
    '--forwarder-result',
  );
}

async function generateNsp(request: InstallRequest): Promise<Uint8Array> {
  const wasm = Switch.readFileSync('romfs:/hacbrewpack.wasm');
  const main = Switch.readFileSync('romfs:/template/exefs/main');
  const mainNpdm = Switch.readFileSync('romfs:/template/exefs/main.npdm');
  if (!wasm || !main || !mainNpdm) {
    throw new Error('Forwarder generator resources are incomplete.');
  }

  const app = new Switch.Application(request.nroPath);
  if (!app.icon) throw new Error('The shortcut NRO has no embedded icon.');
  const icon = app.icon;
  const titleId = await deterministicTitleId(request.nroPath, request.args);
  const keys = loadForwarderKeys();
  const createModule = (await import('./hacbrewpack.js')).default;

  return await new Promise<Uint8Array>((resolve, reject) => {
    let module: Module;
    void createModule({
      noInitialRun: true,
      // Emscripten defaults to console.log/error. On Switch that activates the
      // text console over the canvas, producing a black screen with raw output.
      // Progress belongs to the game overlay; failures use the result channel.
      print: () => {},
      printErr: () => {},
      wasmBinary: wasm,
      preRun(instance: Module) {
        module = instance;
        const { FS } = instance;
        FS.writeFile('/keys.dat', keys);
        FS.mkdir('/exefs');
        FS.writeFile('/exefs/main', new Uint8Array(main));
        FS.writeFile('/exefs/main.npdm', new Uint8Array(mainNpdm));

        const nacp = new NACP();
        nacp.id = titleId;
        nacp.title = request.name;
        nacp.author = request.author || 'Blade-Punisher';
        nacp.version = request.displayVersion || '0.0.6';
        nacp.startupUserAccount = 0;
        nacp.userAccountSaveDataSize = 0n;
        nacp.userAccountSaveDataJournalSize = 0n;

        FS.mkdir('/control');
        FS.writeFile('/control/control.nacp', new Uint8Array(nacp.buffer));
        FS.writeFile('/control/icon_AmericanEnglish.dat', new Uint8Array(icon));
        FS.mkdir('/romfs');
        FS.writeFile('/romfs/nextArgv', new TextEncoder().encode(request.args));
        FS.writeFile(
          '/romfs/nextNroPath',
          new TextEncoder().encode(request.nroPath),
        );
      },
      onRuntimeInitialized() {
        try {
          const exitCode = module.callMain([
            '--nopatchnacplogo',
            '--titleid',
            titleId,
            '--nologo',
            '--plaintext',
          ]);
          if (exitCode !== 0) {
            reject(new Error(`Forwarder generator exited with code ${exitCode}.`));
            return;
          }
          const name = module.FS
            .readdir('/hacbrewpack_nsp')
            .find((entry) => entry.endsWith('.nsp'));
          if (!name) throw new Error('Generated NSP could not be found.');
          resolve(module.FS.readFile(`/hacbrewpack_nsp/${name}`));
        } catch (error) {
          reject(error);
        }
      },
    }).catch(reject);
  });
}

function writeResult(result: object) {
  try { Switch.writeFileSync(resultPath, JSON.stringify(result)); trace('result_written'); }
  catch { trace('result_write_failed'); }
}
async function install() {
  // Always show the progress panel before artwork decoding or synchronous WASM.
  await waitForPaint(); await waitForPaint();
  await prepareVisibleInstaller(loadBackground, waitForPaint);
  trace('visible_preparation_ready');
  if (Switch.appletType() !== AppletType.Application)
    throw new Error('Full-memory/application mode is required to install a forwarder.');
  const request = readRequest();
  trace('request_valid');
  stage = 1;
  await waitForPaint(); await waitForPaint();
  trace('nsp_generate_begin');
  const nsp = await generateNsp(request);
  trace('nsp_generate_done');
  stage = 2;
  await waitForPaint(); await waitForPaint();
  trace('home_install_begin');
  for await (const _event of installNsp(new Blob([nsp]), NcmStorageId.SdCard)) { }
  trace('home_install_done');
  writeResult({ version: 1, success: true, name: request.name, installedAt: Date.now(), automatic: !!request.automatic });
  stage = 3;
  await returnToSwitchNow(request);
}
void install().catch(async (error: unknown) => {
  trace('install_failed');
  const message = error instanceof Error ? error.message : String(error);
  let automatic = false; let name = '';
  try { const r = readRequest(); automatic = !!r.automatic; name = r.name; } catch { }
  writeResult({ version: 1, success: false, name, error: message, automatic });
  failed = true;
  try { await returnToSwitchNow(readRequest()); }
  catch { trace('return_failed'); exitEnabled = true; }
});
