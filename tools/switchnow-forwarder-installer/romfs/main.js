var __create = Object.create;
var __defProp = Object.defineProperty;
var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
var __getOwnPropNames = Object.getOwnPropertyNames;
var __getProtoOf = Object.getPrototypeOf;
var __hasOwnProp = Object.prototype.hasOwnProperty;
var __require = /* @__PURE__ */ ((x) => typeof require !== "undefined" ? require : typeof Proxy !== "undefined" ? new Proxy(x, {
  get: (a, b) => (typeof require !== "undefined" ? require : a)[b]
}) : x)(function(x) {
  if (typeof require !== "undefined") return require.apply(this, arguments);
  throw Error('Dynamic require of "' + x + '" is not supported');
});
var __commonJS = (cb, mod) => function __require2() {
  return mod || (0, cb[__getOwnPropNames(cb)[0]])((mod = { exports: {} }).exports, mod), mod.exports;
};
var __copyProps = (to, from, except, desc) => {
  if (from && typeof from === "object" || typeof from === "function") {
    for (let key of __getOwnPropNames(from))
      if (!__hasOwnProp.call(to, key) && key !== except)
        __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
  }
  return to;
};
var __toESM = (mod, isNodeMode, target) => (target = mod != null ? __create(__getProtoOf(mod)) : {}, __copyProps(
  // If the importer is in node compatibility mode or this is not an ESM
  // file that has been converted to a CommonJS file using a Babel-
  // compatible transform (i.e. "__esModule" has not been set), then set
  // "default" to the CommonJS "module.exports" for node compatibility.
  isNodeMode || !mod || !mod.__esModule ? __defProp(target, "default", { value: mod, enumerable: true }) : target,
  mod
));

// src/hacbrewpack.js
var require_hacbrewpack = __commonJS({
  "src/hacbrewpack.js"(exports, module) {
    "use strict";
    var hacbrewpack = (() => {
      var _scriptName = typeof document != "undefined" ? document.currentScript?.src : void 0;
      return function(moduleArg = {}) {
        var moduleRtn;
        var Module = moduleArg;
        var readyPromiseResolve, readyPromiseReject;
        var readyPromise = new Promise((resolve, reject) => {
          readyPromiseResolve = resolve;
          readyPromiseReject = reject;
        });
        [
          "_memory",
          "___indirect_function_table",
          "_main",
          "onRuntimeInitialized"
        ].forEach((prop) => {
          if (!Object.getOwnPropertyDescriptor(readyPromise, prop)) {
            Object.defineProperty(readyPromise, prop, {
              get: () => abort(
                "You are getting " + prop + " on the Promise object, instead of the instance. Use .then() to get called back with the instance, see the MODULARIZE docs in src/settings.js"
              ),
              set: () => abort(
                "You are setting " + prop + " on the Promise object, instead of the instance. Use .then() to get called back with the instance, see the MODULARIZE docs in src/settings.js"
              )
            });
          }
        });
        var ENVIRONMENT_IS_WEB = true;
        var ENVIRONMENT_IS_WORKER = false;
        var ENVIRONMENT_IS_NODE = false;
        var ENVIRONMENT_IS_SHELL = false;
        var moduleOverrides = Object.assign({}, Module);
        var arguments_ = [];
        var thisProgram = "./this.program";
        var quit_ = (status, toThrow) => {
          throw toThrow;
        };
        var scriptDirectory = "";
        function locateFile(path) {
          if (Module["locateFile"]) {
            return Module["locateFile"](path, scriptDirectory);
          }
          return scriptDirectory + path;
        }
        var readAsync, readBinary;
        if (ENVIRONMENT_IS_SHELL) {
          if (typeof process == "object" && typeof __require === "function" || typeof window == "object" || typeof importScripts == "function")
            throw new Error(
              "not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)"
            );
        } else if (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER) {
          if (ENVIRONMENT_IS_WORKER) {
            scriptDirectory = self.location.href;
          } else if (typeof document != "undefined" && document.currentScript) {
            scriptDirectory = document.currentScript.src;
          }
          if (_scriptName) {
            scriptDirectory = _scriptName;
          }
          if (scriptDirectory.startsWith("blob:")) {
            scriptDirectory = "";
          } else {
            scriptDirectory = scriptDirectory.substr(
              0,
              scriptDirectory.replace(/[?#].*/, "").lastIndexOf("/") + 1
            );
          }
          if (!(typeof window == "object" || typeof importScripts == "function"))
            throw new Error(
              "not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)"
            );
          {
            readAsync = (url) => {
              assert(!isFileURI(url), "readAsync does not work with file:// URLs");
              return fetch(url, { credentials: "same-origin" }).then((response) => {
                if (response.ok) {
                  return response.arrayBuffer();
                }
                return Promise.reject(
                  new Error(response.status + " : " + response.url)
                );
              });
            };
          }
        } else {
          throw new Error("environment detection error");
        }
        var out = Module["print"] || console.log.bind(console);
        var err = Module["printErr"] || console.error.bind(console);
        Object.assign(Module, moduleOverrides);
        moduleOverrides = null;
        checkIncomingModuleAPI();
        if (Module["arguments"]) arguments_ = Module["arguments"];
        legacyModuleProp("arguments", "arguments_");
        if (Module["thisProgram"]) thisProgram = Module["thisProgram"];
        legacyModuleProp("thisProgram", "thisProgram");
        assert(
          typeof Module["memoryInitializerPrefixURL"] == "undefined",
          "Module.memoryInitializerPrefixURL option was removed, use Module.locateFile instead"
        );
        assert(
          typeof Module["pthreadMainPrefixURL"] == "undefined",
          "Module.pthreadMainPrefixURL option was removed, use Module.locateFile instead"
        );
        assert(
          typeof Module["cdInitializerPrefixURL"] == "undefined",
          "Module.cdInitializerPrefixURL option was removed, use Module.locateFile instead"
        );
        assert(
          typeof Module["filePackagePrefixURL"] == "undefined",
          "Module.filePackagePrefixURL option was removed, use Module.locateFile instead"
        );
        assert(
          typeof Module["read"] == "undefined",
          "Module.read option was removed"
        );
        assert(
          typeof Module["readAsync"] == "undefined",
          "Module.readAsync option was removed (modify readAsync in JS)"
        );
        assert(
          typeof Module["readBinary"] == "undefined",
          "Module.readBinary option was removed (modify readBinary in JS)"
        );
        assert(
          typeof Module["setWindowTitle"] == "undefined",
          "Module.setWindowTitle option was removed (modify emscripten_set_window_title in JS)"
        );
        assert(
          typeof Module["TOTAL_MEMORY"] == "undefined",
          "Module.TOTAL_MEMORY has been renamed Module.INITIAL_MEMORY"
        );
        legacyModuleProp("asm", "wasmExports");
        legacyModuleProp("readAsync", "readAsync");
        legacyModuleProp("readBinary", "readBinary");
        legacyModuleProp("setWindowTitle", "setWindowTitle");
        var IDBFS = "IDBFS is no longer included by default; build with -lidbfs.js";
        var PROXYFS = "PROXYFS is no longer included by default; build with -lproxyfs.js";
        var WORKERFS = "WORKERFS is no longer included by default; build with -lworkerfs.js";
        var FETCHFS = "FETCHFS is no longer included by default; build with -lfetchfs.js";
        var ICASEFS = "ICASEFS is no longer included by default; build with -licasefs.js";
        var JSFILEFS = "JSFILEFS is no longer included by default; build with -ljsfilefs.js";
        var OPFS = "OPFS is no longer included by default; build with -lopfs.js";
        var NODEFS = "NODEFS is no longer included by default; build with -lnodefs.js";
        assert(
          !ENVIRONMENT_IS_WORKER,
          "worker environment detected but not enabled at build time.  Add `worker` to `-sENVIRONMENT` to enable."
        );
        assert(
          !ENVIRONMENT_IS_NODE,
          "node environment detected but not enabled at build time.  Add `node` to `-sENVIRONMENT` to enable."
        );
        assert(
          !ENVIRONMENT_IS_SHELL,
          "shell environment detected but not enabled at build time.  Add `shell` to `-sENVIRONMENT` to enable."
        );
        var wasmBinary = Module["wasmBinary"];
        legacyModuleProp("wasmBinary", "wasmBinary");
        if (typeof WebAssembly != "object") {
          err("no native wasm support detected");
        }
        var wasmMemory;
        var ABORT = false;
        var EXITSTATUS;
        function assert(condition, text) {
          if (!condition) {
            abort("Assertion failed" + (text ? ": " + text : ""));
          }
        }
        function _malloc() {
          abort(
            "malloc() called but not included in the build - add `_malloc` to EXPORTED_FUNCTIONS"
          );
        }
        function _free() {
          abort(
            "free() called but not included in the build - add `_free` to EXPORTED_FUNCTIONS"
          );
        }
        var HEAP, HEAP8, HEAPU8, HEAP16, HEAPU16, HEAP32, HEAPU32, HEAPF32, HEAPF64;
        function updateMemoryViews() {
          var b = wasmMemory.buffer;
          Module["HEAP8"] = HEAP8 = new Int8Array(b);
          Module["HEAP16"] = HEAP16 = new Int16Array(b);
          Module["HEAPU8"] = HEAPU8 = new Uint8Array(b);
          Module["HEAPU16"] = HEAPU16 = new Uint16Array(b);
          Module["HEAP32"] = HEAP32 = new Int32Array(b);
          Module["HEAPU32"] = HEAPU32 = new Uint32Array(b);
          Module["HEAPF32"] = HEAPF32 = new Float32Array(b);
          Module["HEAPF64"] = HEAPF64 = new Float64Array(b);
        }
        assert(
          !Module["STACK_SIZE"],
          "STACK_SIZE can no longer be set at runtime.  Use -sSTACK_SIZE at link time"
        );
        assert(
          typeof Int32Array != "undefined" && typeof Float64Array !== "undefined" && Int32Array.prototype.subarray != void 0 && Int32Array.prototype.set != void 0,
          "JS engine does not provide full typed array support"
        );
        assert(
          !Module["wasmMemory"],
          "Use of `wasmMemory` detected.  Use -sIMPORTED_MEMORY to define wasmMemory externally"
        );
        assert(
          !Module["INITIAL_MEMORY"],
          "Detected runtime INITIAL_MEMORY setting.  Use -sIMPORTED_MEMORY to define wasmMemory dynamically"
        );
        function writeStackCookie() {
          var max = _emscripten_stack_get_end();
          assert((max & 3) == 0);
          if (max == 0) {
            max += 4;
          }
          HEAPU32[max >> 2] = 34821223;
          HEAPU32[max + 4 >> 2] = 2310721022;
          HEAPU32[0 >> 2] = 1668509029;
        }
        function checkStackCookie() {
          if (ABORT) return;
          var max = _emscripten_stack_get_end();
          if (max == 0) {
            max += 4;
          }
          var cookie1 = HEAPU32[max >> 2];
          var cookie2 = HEAPU32[max + 4 >> 2];
          if (cookie1 != 34821223 || cookie2 != 2310721022) {
            abort(
              `Stack overflow! Stack cookie has been overwritten at ${ptrToString(max)}, expected hex dwords 0x89BACDFE and 0x2135467, but received ${ptrToString(cookie2)} ${ptrToString(cookie1)}`
            );
          }
          if (HEAPU32[0 >> 2] != 1668509029) {
            abort(
              "Runtime error: The application has corrupted its heap memory area (address zero)!"
            );
          }
        }
        var __ATPRERUN__ = [];
        var __ATINIT__ = [];
        var __ATMAIN__ = [];
        var __ATEXIT__ = [];
        var __ATPOSTRUN__ = [];
        var runtimeInitialized = false;
        function preRun() {
          var preRuns = Module["preRun"];
          if (preRuns) {
            if (typeof preRuns == "function") preRuns = [preRuns];
            preRuns.forEach(addOnPreRun);
          }
          callRuntimeCallbacks(__ATPRERUN__);
        }
        function initRuntime() {
          assert(!runtimeInitialized);
          runtimeInitialized = true;
          checkStackCookie();
          if (!Module["noFSInit"] && !FS.initialized) FS.init();
          FS.ignorePermissions = false;
          TTY.init();
          callRuntimeCallbacks(__ATINIT__);
        }
        function preMain() {
          checkStackCookie();
          callRuntimeCallbacks(__ATMAIN__);
        }
        function postRun() {
          checkStackCookie();
          var postRuns = Module["postRun"];
          if (postRuns) {
            if (typeof postRuns == "function") postRuns = [postRuns];
            postRuns.forEach(addOnPostRun);
          }
          callRuntimeCallbacks(__ATPOSTRUN__);
        }
        function addOnPreRun(cb) {
          __ATPRERUN__.unshift(cb);
        }
        function addOnInit(cb) {
          __ATINIT__.unshift(cb);
        }
        function addOnPreMain(cb) {
          __ATMAIN__.unshift(cb);
        }
        function addOnExit(cb) {
        }
        function addOnPostRun(cb) {
          __ATPOSTRUN__.unshift(cb);
        }
        assert(
          Math.imul,
          "This browser does not support Math.imul(), build with LEGACY_VM_SUPPORT or POLYFILL_OLD_MATH_FUNCTIONS to add in a polyfill"
        );
        assert(
          Math.fround,
          "This browser does not support Math.fround(), build with LEGACY_VM_SUPPORT or POLYFILL_OLD_MATH_FUNCTIONS to add in a polyfill"
        );
        assert(
          Math.clz32,
          "This browser does not support Math.clz32(), build with LEGACY_VM_SUPPORT or POLYFILL_OLD_MATH_FUNCTIONS to add in a polyfill"
        );
        assert(
          Math.trunc,
          "This browser does not support Math.trunc(), build with LEGACY_VM_SUPPORT or POLYFILL_OLD_MATH_FUNCTIONS to add in a polyfill"
        );
        var runDependencies = 0;
        var runDependencyWatcher = null;
        var dependenciesFulfilled = null;
        var runDependencyTracking = {};
        function getUniqueRunDependency(id) {
          var orig = id;
          while (1) {
            if (!runDependencyTracking[id]) return id;
            id = orig + Math.random();
          }
        }
        function addRunDependency(id) {
          runDependencies++;
          Module["monitorRunDependencies"]?.(runDependencies);
          if (id) {
            assert(!runDependencyTracking[id]);
            runDependencyTracking[id] = 1;
            if (runDependencyWatcher === null && typeof setInterval != "undefined") {
              runDependencyWatcher = setInterval(() => {
                if (ABORT) {
                  clearInterval(runDependencyWatcher);
                  runDependencyWatcher = null;
                  return;
                }
                var shown = false;
                for (var dep in runDependencyTracking) {
                  if (!shown) {
                    shown = true;
                    err("still waiting on run dependencies:");
                  }
                  err(`dependency: ${dep}`);
                }
                if (shown) {
                  err("(end of list)");
                }
              }, 1e4);
            }
          } else {
            err("warning: run dependency added without ID");
          }
        }
        function removeRunDependency(id) {
          runDependencies--;
          Module["monitorRunDependencies"]?.(runDependencies);
          if (id) {
            assert(runDependencyTracking[id]);
            delete runDependencyTracking[id];
          } else {
            err("warning: run dependency removed without ID");
          }
          if (runDependencies == 0) {
            if (runDependencyWatcher !== null) {
              clearInterval(runDependencyWatcher);
              runDependencyWatcher = null;
            }
            if (dependenciesFulfilled) {
              var callback = dependenciesFulfilled;
              dependenciesFulfilled = null;
              callback();
            }
          }
        }
        function abort(what) {
          Module["onAbort"]?.(what);
          what = "Aborted(" + what + ")";
          err(what);
          ABORT = true;
          var e = new WebAssembly.RuntimeError(what);
          readyPromiseReject(e);
          throw e;
        }
        var dataURIPrefix = "data:application/octet-stream;base64,";
        var isDataURI = (filename) => filename.startsWith(dataURIPrefix);
        var isFileURI = (filename) => filename.startsWith("file://");
        function createExportWrapper(name, nargs) {
          return (...args) => {
            assert(
              runtimeInitialized,
              `native function \`${name}\` called before runtime initialization`
            );
            var f = wasmExports[name];
            assert(f, `exported native function \`${name}\` not found`);
            assert(
              args.length <= nargs,
              `native function \`${name}\` called with ${args.length} args but expects ${nargs}`
            );
            return f(...args);
          };
        }
        function findWasmBinary() {
          var f = "hacbrewpack.wasm";
          if (!isDataURI(f)) {
            return locateFile(f);
          }
          return f;
        }
        var wasmBinaryFile;
        function getBinarySync(file) {
          if (file == wasmBinaryFile && wasmBinary) {
            return new Uint8Array(wasmBinary);
          }
          if (readBinary) {
            return readBinary(file);
          }
          throw "both async and sync fetching of the wasm failed";
        }
        function getBinaryPromise(binaryFile) {
          if (!wasmBinary) {
            return readAsync(binaryFile).then(
              (response) => new Uint8Array(
                /** @type{!ArrayBuffer} */
                response
              ),
              // Fall back to getBinarySync if readAsync fails
              () => getBinarySync(binaryFile)
            );
          }
          return Promise.resolve().then(() => getBinarySync(binaryFile));
        }
        function instantiateArrayBuffer(binaryFile, imports, receiver) {
          return getBinaryPromise(binaryFile).then((binary) => {
            return WebAssembly.instantiate(binary, imports);
          }).then(receiver, (reason) => {
            err(`failed to asynchronously prepare wasm: ${reason}`);
            if (isFileURI(wasmBinaryFile)) {
              err(
                `warning: Loading from a file URI (${wasmBinaryFile}) is not supported in most browsers. See https://emscripten.org/docs/getting_started/FAQ.html#how-do-i-run-a-local-webserver-for-testing-why-does-my-program-stall-in-downloading-or-preparing`
              );
            }
            abort(reason);
          });
        }
        function instantiateAsync(binary, binaryFile, imports, callback) {
          if (!binary && typeof WebAssembly.instantiateStreaming == "function" && !isDataURI(binaryFile) && typeof fetch == "function") {
            return fetch(binaryFile, { credentials: "same-origin" }).then(
              (response) => {
                var result = WebAssembly.instantiateStreaming(response, imports);
                return result.then(callback, function(reason) {
                  err(`wasm streaming compile failed: ${reason}`);
                  err("falling back to ArrayBuffer instantiation");
                  return instantiateArrayBuffer(binaryFile, imports, callback);
                });
              }
            );
          }
          return instantiateArrayBuffer(binaryFile, imports, callback);
        }
        function getWasmImports() {
          return {
            env: wasmImports,
            wasi_snapshot_preview1: wasmImports
          };
        }
        function createWasm() {
          var info = getWasmImports();
          function receiveInstance(instance, module2) {
            wasmExports = instance.exports;
            wasmMemory = wasmExports["memory"];
            assert(wasmMemory, "memory not found in wasm exports");
            updateMemoryViews();
            wasmTable = wasmExports["__indirect_function_table"];
            assert(wasmTable, "table not found in wasm exports");
            addOnInit(wasmExports["__wasm_call_ctors"]);
            removeRunDependency("wasm-instantiate");
            return wasmExports;
          }
          addRunDependency("wasm-instantiate");
          var trueModule = Module;
          function receiveInstantiationResult(result) {
            assert(
              Module === trueModule,
              "the Module object should not be replaced during async compilation - perhaps the order of HTML elements is wrong?"
            );
            trueModule = null;
            receiveInstance(result["instance"]);
          }
          if (Module["instantiateWasm"]) {
            try {
              return Module["instantiateWasm"](info, receiveInstance);
            } catch (e) {
              err(`Module.instantiateWasm callback failed with error: ${e}`);
              readyPromiseReject(e);
            }
          }
          wasmBinaryFile ??= findWasmBinary();
          instantiateAsync(
            wasmBinary,
            wasmBinaryFile,
            info,
            receiveInstantiationResult
          ).catch(readyPromiseReject);
          return {};
        }
        var tempDouble;
        var tempI64;
        (() => {
          var h16 = new Int16Array(1);
          var h8 = new Int8Array(h16.buffer);
          h16[0] = 25459;
          if (h8[0] !== 115 || h8[1] !== 99)
            throw "Runtime error: expected the system to be little-endian! (Run with -sSUPPORT_BIG_ENDIAN to bypass)";
        })();
        if (Module["ENVIRONMENT"]) {
          throw new Error(
            "Module.ENVIRONMENT has been deprecated. To force the environment, use the ENVIRONMENT compile-time option (for example, -sENVIRONMENT=web or -sENVIRONMENT=node)"
          );
        }
        function legacyModuleProp(prop, newName, incoming = true) {
          if (!Object.getOwnPropertyDescriptor(Module, prop)) {
            Object.defineProperty(Module, prop, {
              configurable: true,
              get() {
                let extra = incoming ? " (the initial value can be provided on Module, but after startup the value is only looked for on a local variable of that name)" : "";
                abort(
                  `\`Module.${prop}\` has been replaced by \`${newName}\`` + extra
                );
              }
            });
          }
        }
        function ignoredModuleProp(prop) {
          if (Object.getOwnPropertyDescriptor(Module, prop)) {
            abort(
              `\`Module.${prop}\` was supplied but \`${prop}\` not included in INCOMING_MODULE_JS_API`
            );
          }
        }
        function isExportedByForceFilesystem(name) {
          return name === "FS_createPath" || name === "FS_createDataFile" || name === "FS_createPreloadedFile" || name === "FS_unlink" || name === "addRunDependency" || // The old FS has some functionality that WasmFS lacks.
          name === "FS_createLazyFile" || name === "FS_createDevice" || name === "removeRunDependency";
        }
        function hookGlobalSymbolAccess(sym, func) {
        }
        function missingGlobal(sym, msg) {
          hookGlobalSymbolAccess(sym, () => {
            warnOnce(`\`${sym}\` is not longer defined by emscripten. ${msg}`);
          });
        }
        missingGlobal("buffer", "Please use HEAP8.buffer or wasmMemory.buffer");
        missingGlobal("asm", "Please use wasmExports instead");
        function missingLibrarySymbol(sym) {
          hookGlobalSymbolAccess(sym, () => {
            var msg = `\`${sym}\` is a library symbol and not included by default; add it to your library.js __deps or to DEFAULT_LIBRARY_FUNCS_TO_INCLUDE on the command line`;
            var librarySymbol = sym;
            if (!librarySymbol.startsWith("_")) {
              librarySymbol = "$" + sym;
            }
            msg += ` (e.g. -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE='${librarySymbol}')`;
            if (isExportedByForceFilesystem(sym)) {
              msg += ". Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you";
            }
            warnOnce(msg);
          });
          unexportedRuntimeSymbol(sym);
        }
        function unexportedRuntimeSymbol(sym) {
          if (!Object.getOwnPropertyDescriptor(Module, sym)) {
            Object.defineProperty(Module, sym, {
              configurable: true,
              get() {
                var msg = `'${sym}' was not exported. add it to EXPORTED_RUNTIME_METHODS (see the Emscripten FAQ)`;
                if (isExportedByForceFilesystem(sym)) {
                  msg += ". Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you";
                }
                abort(msg);
              }
            });
          }
        }
        function dbg(...args) {
          console.warn(...args);
        }
        function ExitStatus(status) {
          this.name = "ExitStatus";
          this.message = `Program terminated with exit(${status})`;
          this.status = status;
        }
        var callRuntimeCallbacks = (callbacks) => {
          callbacks.forEach((f) => f(Module));
        };
        function getValue(ptr, type = "i8") {
          if (type.endsWith("*")) type = "*";
          switch (type) {
            case "i1":
              return HEAP8[ptr];
            case "i8":
              return HEAP8[ptr];
            case "i16":
              return HEAP16[ptr >> 1];
            case "i32":
              return HEAP32[ptr >> 2];
            case "i64":
              abort("to do getValue(i64) use WASM_BIGINT");
            case "float":
              return HEAPF32[ptr >> 2];
            case "double":
              return HEAPF64[ptr >> 3];
            case "*":
              return HEAPU32[ptr >> 2];
            default:
              abort(`invalid type for getValue: ${type}`);
          }
        }
        var noExitRuntime = Module["noExitRuntime"] || true;
        var ptrToString = (ptr) => {
          assert(typeof ptr === "number");
          ptr >>>= 0;
          return "0x" + ptr.toString(16).padStart(8, "0");
        };
        function setValue(ptr, value, type = "i8") {
          if (type.endsWith("*")) type = "*";
          switch (type) {
            case "i1":
              HEAP8[ptr] = value;
              break;
            case "i8":
              HEAP8[ptr] = value;
              break;
            case "i16":
              HEAP16[ptr >> 1] = value;
              break;
            case "i32":
              HEAP32[ptr >> 2] = value;
              break;
            case "i64":
              abort("to do setValue(i64) use WASM_BIGINT");
            case "float":
              HEAPF32[ptr >> 2] = value;
              break;
            case "double":
              HEAPF64[ptr >> 3] = value;
              break;
            case "*":
              HEAPU32[ptr >> 2] = value;
              break;
            default:
              abort(`invalid type for setValue: ${type}`);
          }
        }
        var stackRestore = (val) => __emscripten_stack_restore(val);
        var stackSave = () => _emscripten_stack_get_current();
        var warnOnce = (text) => {
          warnOnce.shown ||= {};
          if (!warnOnce.shown[text]) {
            warnOnce.shown[text] = 1;
            err(text);
          }
        };
        var UTF8Decoder = typeof TextDecoder != "undefined" ? new TextDecoder() : void 0;
        var UTF8ArrayToString = (heapOrArray, idx = 0, maxBytesToRead = NaN) => {
          var endIdx = idx + maxBytesToRead;
          var endPtr = idx;
          while (heapOrArray[endPtr] && !(endPtr >= endIdx)) ++endPtr;
          if (endPtr - idx > 16 && heapOrArray.buffer && UTF8Decoder) {
            return UTF8Decoder.decode(heapOrArray.subarray(idx, endPtr));
          }
          var str = "";
          while (idx < endPtr) {
            var u0 = heapOrArray[idx++];
            if (!(u0 & 128)) {
              str += String.fromCharCode(u0);
              continue;
            }
            var u1 = heapOrArray[idx++] & 63;
            if ((u0 & 224) == 192) {
              str += String.fromCharCode((u0 & 31) << 6 | u1);
              continue;
            }
            var u2 = heapOrArray[idx++] & 63;
            if ((u0 & 240) == 224) {
              u0 = (u0 & 15) << 12 | u1 << 6 | u2;
            } else {
              if ((u0 & 248) != 240)
                warnOnce(
                  "Invalid UTF-8 leading byte " + ptrToString(u0) + " encountered when deserializing a UTF-8 string in wasm memory to a JS string!"
                );
              u0 = (u0 & 7) << 18 | u1 << 12 | u2 << 6 | heapOrArray[idx++] & 63;
            }
            if (u0 < 65536) {
              str += String.fromCharCode(u0);
            } else {
              var ch = u0 - 65536;
              str += String.fromCharCode(
                55296 | ch >> 10,
                56320 | ch & 1023
              );
            }
          }
          return str;
        };
        var UTF8ToString = (ptr, maxBytesToRead) => {
          assert(
            typeof ptr == "number",
            `UTF8ToString expects a number (got ${typeof ptr})`
          );
          return ptr ? UTF8ArrayToString(HEAPU8, ptr, maxBytesToRead) : "";
        };
        var ___assert_fail = (condition, filename, line, func) => {
          abort(
            `Assertion failed: ${UTF8ToString(condition)}, at: ` + [
              filename ? UTF8ToString(filename) : "unknown filename",
              line,
              func ? UTF8ToString(func) : "unknown function"
            ]
          );
        };
        var wasmTableMirror = [];
        var wasmTable;
        var getWasmTableEntry = (funcPtr) => {
          var func = wasmTableMirror[funcPtr];
          if (!func) {
            if (funcPtr >= wasmTableMirror.length)
              wasmTableMirror.length = funcPtr + 1;
            wasmTableMirror[funcPtr] = func = wasmTable.get(funcPtr);
          }
          assert(
            wasmTable.get(funcPtr) == func,
            "JavaScript-side Wasm function table mirror is out of date!"
          );
          return func;
        };
        var ___call_sighandler = (fp, sig) => getWasmTableEntry(fp)(sig);
        function syscallGetVarargI() {
          assert(SYSCALLS.varargs != void 0);
          var ret = HEAP32[+SYSCALLS.varargs >> 2];
          SYSCALLS.varargs += 4;
          return ret;
        }
        var syscallGetVarargP = syscallGetVarargI;
        var PATH = {
          isAbs: (path) => path.charAt(0) === "/",
          splitPath: (filename) => {
            var splitPathRe = /^(\/?|)([\s\S]*?)((?:\.{1,2}|[^\/]+?|)(\.[^.\/]*|))(?:[\/]*)$/;
            return splitPathRe.exec(filename).slice(1);
          },
          normalizeArray: (parts, allowAboveRoot) => {
            var up = 0;
            for (var i = parts.length - 1; i >= 0; i--) {
              var last = parts[i];
              if (last === ".") {
                parts.splice(i, 1);
              } else if (last === "..") {
                parts.splice(i, 1);
                up++;
              } else if (up) {
                parts.splice(i, 1);
                up--;
              }
            }
            if (allowAboveRoot) {
              for (; up; up--) {
                parts.unshift("..");
              }
            }
            return parts;
          },
          normalize: (path) => {
            var isAbsolute = PATH.isAbs(path), trailingSlash = path.substr(-1) === "/";
            path = PATH.normalizeArray(
              path.split("/").filter((p) => !!p),
              !isAbsolute
            ).join("/");
            if (!path && !isAbsolute) {
              path = ".";
            }
            if (path && trailingSlash) {
              path += "/";
            }
            return (isAbsolute ? "/" : "") + path;
          },
          dirname: (path) => {
            var result = PATH.splitPath(path), root = result[0], dir = result[1];
            if (!root && !dir) {
              return ".";
            }
            if (dir) {
              dir = dir.substr(0, dir.length - 1);
            }
            return root + dir;
          },
          basename: (path) => {
            if (path === "/") return "/";
            path = PATH.normalize(path);
            path = path.replace(/\/$/, "");
            var lastSlash = path.lastIndexOf("/");
            if (lastSlash === -1) return path;
            return path.substr(lastSlash + 1);
          },
          join: (...paths) => PATH.normalize(paths.join("/")),
          join2: (l, r) => PATH.normalize(l + "/" + r)
        };
        var initRandomFill = () => {
          if (typeof crypto == "object" && typeof crypto["getRandomValues"] == "function") {
            return (view2) => crypto.getRandomValues(view2);
          } else
            abort(
              "no cryptographic support found for randomDevice. consider polyfilling it if you want to use something insecure like Math.random(), e.g. put this in a --pre-js: var crypto = { getRandomValues: (array) => { for (var i = 0; i < array.length; i++) array[i] = (Math.random()*256)|0 } };"
            );
        };
        var randomFill = (view2) => {
          return (randomFill = initRandomFill())(view2);
        };
        var PATH_FS = {
          resolve: (...args) => {
            var resolvedPath = "", resolvedAbsolute = false;
            for (var i = args.length - 1; i >= -1 && !resolvedAbsolute; i--) {
              var path = i >= 0 ? args[i] : FS.cwd();
              if (typeof path != "string") {
                throw new TypeError("Arguments to path.resolve must be strings");
              } else if (!path) {
                return "";
              }
              resolvedPath = path + "/" + resolvedPath;
              resolvedAbsolute = PATH.isAbs(path);
            }
            resolvedPath = PATH.normalizeArray(
              resolvedPath.split("/").filter((p) => !!p),
              !resolvedAbsolute
            ).join("/");
            return (resolvedAbsolute ? "/" : "") + resolvedPath || ".";
          },
          relative: (from, to) => {
            from = PATH_FS.resolve(from).substr(1);
            to = PATH_FS.resolve(to).substr(1);
            function trim(arr) {
              var start = 0;
              for (; start < arr.length; start++) {
                if (arr[start] !== "") break;
              }
              var end = arr.length - 1;
              for (; end >= 0; end--) {
                if (arr[end] !== "") break;
              }
              if (start > end) return [];
              return arr.slice(start, end - start + 1);
            }
            var fromParts = trim(from.split("/"));
            var toParts = trim(to.split("/"));
            var length = Math.min(fromParts.length, toParts.length);
            var samePartsLength = length;
            for (var i = 0; i < length; i++) {
              if (fromParts[i] !== toParts[i]) {
                samePartsLength = i;
                break;
              }
            }
            var outputParts = [];
            for (var i = samePartsLength; i < fromParts.length; i++) {
              outputParts.push("..");
            }
            outputParts = outputParts.concat(toParts.slice(samePartsLength));
            return outputParts.join("/");
          }
        };
        var FS_stdin_getChar_buffer = [];
        var lengthBytesUTF8 = (str) => {
          var len = 0;
          for (var i = 0; i < str.length; ++i) {
            var c = str.charCodeAt(i);
            if (c <= 127) {
              len++;
            } else if (c <= 2047) {
              len += 2;
            } else if (c >= 55296 && c <= 57343) {
              len += 4;
              ++i;
            } else {
              len += 3;
            }
          }
          return len;
        };
        var stringToUTF8Array = (str, heap, outIdx, maxBytesToWrite) => {
          assert(
            typeof str === "string",
            `stringToUTF8Array expects a string (got ${typeof str})`
          );
          if (!(maxBytesToWrite > 0)) return 0;
          var startIdx = outIdx;
          var endIdx = outIdx + maxBytesToWrite - 1;
          for (var i = 0; i < str.length; ++i) {
            var u = str.charCodeAt(i);
            if (u >= 55296 && u <= 57343) {
              var u1 = str.charCodeAt(++i);
              u = 65536 + ((u & 1023) << 10) | u1 & 1023;
            }
            if (u <= 127) {
              if (outIdx >= endIdx) break;
              heap[outIdx++] = u;
            } else if (u <= 2047) {
              if (outIdx + 1 >= endIdx) break;
              heap[outIdx++] = 192 | u >> 6;
              heap[outIdx++] = 128 | u & 63;
            } else if (u <= 65535) {
              if (outIdx + 2 >= endIdx) break;
              heap[outIdx++] = 224 | u >> 12;
              heap[outIdx++] = 128 | u >> 6 & 63;
              heap[outIdx++] = 128 | u & 63;
            } else {
              if (outIdx + 3 >= endIdx) break;
              if (u > 1114111)
                warnOnce(
                  "Invalid Unicode code point " + ptrToString(u) + " encountered when serializing a JS string to a UTF-8 string in wasm memory! (Valid unicode code points should be in range 0-0x10FFFF)."
                );
              heap[outIdx++] = 240 | u >> 18;
              heap[outIdx++] = 128 | u >> 12 & 63;
              heap[outIdx++] = 128 | u >> 6 & 63;
              heap[outIdx++] = 128 | u & 63;
            }
          }
          heap[outIdx] = 0;
          return outIdx - startIdx;
        };
        function intArrayFromString(stringy, dontAddNull, length) {
          var len = length > 0 ? length : lengthBytesUTF8(stringy) + 1;
          var u8array = new Array(len);
          var numBytesWritten = stringToUTF8Array(
            stringy,
            u8array,
            0,
            u8array.length
          );
          if (dontAddNull) u8array.length = numBytesWritten;
          return u8array;
        }
        var FS_stdin_getChar = () => {
          if (!FS_stdin_getChar_buffer.length) {
            var result = null;
            if (typeof window != "undefined" && typeof window.prompt == "function") {
              result = window.prompt("Input: ");
              if (result !== null) {
                result += "\n";
              }
            } else {
            }
            if (!result) {
              return null;
            }
            FS_stdin_getChar_buffer = intArrayFromString(result, true);
          }
          return FS_stdin_getChar_buffer.shift();
        };
        var TTY = {
          ttys: [],
          init() {
          },
          shutdown() {
          },
          register(dev, ops) {
            TTY.ttys[dev] = { input: [], output: [], ops };
            FS.registerDevice(dev, TTY.stream_ops);
          },
          stream_ops: {
            open(stream) {
              var tty = TTY.ttys[stream.node.rdev];
              if (!tty) {
                throw new FS.ErrnoError(43);
              }
              stream.tty = tty;
              stream.seekable = false;
            },
            close(stream) {
              stream.tty.ops.fsync(stream.tty);
            },
            fsync(stream) {
              stream.tty.ops.fsync(stream.tty);
            },
            read(stream, buffer, offset, length, pos) {
              if (!stream.tty || !stream.tty.ops.get_char) {
                throw new FS.ErrnoError(60);
              }
              var bytesRead = 0;
              for (var i = 0; i < length; i++) {
                var result;
                try {
                  result = stream.tty.ops.get_char(stream.tty);
                } catch (e) {
                  throw new FS.ErrnoError(29);
                }
                if (result === void 0 && bytesRead === 0) {
                  throw new FS.ErrnoError(6);
                }
                if (result === null || result === void 0) break;
                bytesRead++;
                buffer[offset + i] = result;
              }
              if (bytesRead) {
                stream.node.timestamp = Date.now();
              }
              return bytesRead;
            },
            write(stream, buffer, offset, length, pos) {
              if (!stream.tty || !stream.tty.ops.put_char) {
                throw new FS.ErrnoError(60);
              }
              try {
                for (var i = 0; i < length; i++) {
                  stream.tty.ops.put_char(stream.tty, buffer[offset + i]);
                }
              } catch (e) {
                throw new FS.ErrnoError(29);
              }
              if (length) {
                stream.node.timestamp = Date.now();
              }
              return i;
            }
          },
          default_tty_ops: {
            get_char(tty) {
              return FS_stdin_getChar();
            },
            put_char(tty, val) {
              if (val === null || val === 10) {
                out(UTF8ArrayToString(tty.output));
                tty.output = [];
              } else {
                if (val != 0) tty.output.push(val);
              }
            },
            fsync(tty) {
              if (tty.output && tty.output.length > 0) {
                out(UTF8ArrayToString(tty.output));
                tty.output = [];
              }
            },
            ioctl_tcgets(tty) {
              return {
                c_iflag: 25856,
                c_oflag: 5,
                c_cflag: 191,
                c_lflag: 35387,
                c_cc: [
                  3,
                  28,
                  127,
                  21,
                  4,
                  0,
                  1,
                  0,
                  17,
                  19,
                  26,
                  0,
                  18,
                  15,
                  23,
                  22,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0
                ]
              };
            },
            ioctl_tcsets(tty, optional_actions, data) {
              return 0;
            },
            ioctl_tiocgwinsz(tty) {
              return [24, 80];
            }
          },
          default_tty1_ops: {
            put_char(tty, val) {
              if (val === null || val === 10) {
                err(UTF8ArrayToString(tty.output));
                tty.output = [];
              } else {
                if (val != 0) tty.output.push(val);
              }
            },
            fsync(tty) {
              if (tty.output && tty.output.length > 0) {
                err(UTF8ArrayToString(tty.output));
                tty.output = [];
              }
            }
          }
        };
        var zeroMemory = (address, size) => {
          HEAPU8.fill(0, address, address + size);
        };
        var alignMemory = (size, alignment) => {
          assert(alignment, "alignment argument is required");
          return Math.ceil(size / alignment) * alignment;
        };
        var mmapAlloc = (size) => {
          abort(
            "internal error: mmapAlloc called but `emscripten_builtin_memalign` native symbol not exported"
          );
        };
        var MEMFS = {
          ops_table: null,
          mount(mount) {
            return MEMFS.createNode(null, "/", 16384 | 511, 0);
          },
          createNode(parent, name, mode, dev) {
            if (FS.isBlkdev(mode) || FS.isFIFO(mode)) {
              throw new FS.ErrnoError(63);
            }
            MEMFS.ops_table ||= {
              dir: {
                node: {
                  getattr: MEMFS.node_ops.getattr,
                  setattr: MEMFS.node_ops.setattr,
                  lookup: MEMFS.node_ops.lookup,
                  mknod: MEMFS.node_ops.mknod,
                  rename: MEMFS.node_ops.rename,
                  unlink: MEMFS.node_ops.unlink,
                  rmdir: MEMFS.node_ops.rmdir,
                  readdir: MEMFS.node_ops.readdir,
                  symlink: MEMFS.node_ops.symlink
                },
                stream: {
                  llseek: MEMFS.stream_ops.llseek
                }
              },
              file: {
                node: {
                  getattr: MEMFS.node_ops.getattr,
                  setattr: MEMFS.node_ops.setattr
                },
                stream: {
                  llseek: MEMFS.stream_ops.llseek,
                  read: MEMFS.stream_ops.read,
                  write: MEMFS.stream_ops.write,
                  allocate: MEMFS.stream_ops.allocate,
                  mmap: MEMFS.stream_ops.mmap,
                  msync: MEMFS.stream_ops.msync
                }
              },
              link: {
                node: {
                  getattr: MEMFS.node_ops.getattr,
                  setattr: MEMFS.node_ops.setattr,
                  readlink: MEMFS.node_ops.readlink
                },
                stream: {}
              },
              chrdev: {
                node: {
                  getattr: MEMFS.node_ops.getattr,
                  setattr: MEMFS.node_ops.setattr
                },
                stream: FS.chrdev_stream_ops
              }
            };
            var node = FS.createNode(parent, name, mode, dev);
            if (FS.isDir(node.mode)) {
              node.node_ops = MEMFS.ops_table.dir.node;
              node.stream_ops = MEMFS.ops_table.dir.stream;
              node.contents = {};
            } else if (FS.isFile(node.mode)) {
              node.node_ops = MEMFS.ops_table.file.node;
              node.stream_ops = MEMFS.ops_table.file.stream;
              node.usedBytes = 0;
              node.contents = null;
            } else if (FS.isLink(node.mode)) {
              node.node_ops = MEMFS.ops_table.link.node;
              node.stream_ops = MEMFS.ops_table.link.stream;
            } else if (FS.isChrdev(node.mode)) {
              node.node_ops = MEMFS.ops_table.chrdev.node;
              node.stream_ops = MEMFS.ops_table.chrdev.stream;
            }
            node.timestamp = Date.now();
            if (parent) {
              parent.contents[name] = node;
              parent.timestamp = node.timestamp;
            }
            return node;
          },
          getFileDataAsTypedArray(node) {
            if (!node.contents) return new Uint8Array(0);
            if (node.contents.subarray)
              return node.contents.subarray(0, node.usedBytes);
            return new Uint8Array(node.contents);
          },
          expandFileStorage(node, newCapacity) {
            var prevCapacity = node.contents ? node.contents.length : 0;
            if (prevCapacity >= newCapacity) return;
            var CAPACITY_DOUBLING_MAX = 1024 * 1024;
            newCapacity = Math.max(
              newCapacity,
              prevCapacity * (prevCapacity < CAPACITY_DOUBLING_MAX ? 2 : 1.125) >>> 0
            );
            if (prevCapacity != 0) newCapacity = Math.max(newCapacity, 256);
            var oldContents = node.contents;
            node.contents = new Uint8Array(newCapacity);
            if (node.usedBytes > 0)
              node.contents.set(oldContents.subarray(0, node.usedBytes), 0);
          },
          resizeFileStorage(node, newSize) {
            if (node.usedBytes == newSize) return;
            if (newSize == 0) {
              node.contents = null;
              node.usedBytes = 0;
            } else {
              var oldContents = node.contents;
              node.contents = new Uint8Array(newSize);
              if (oldContents) {
                node.contents.set(
                  oldContents.subarray(0, Math.min(newSize, node.usedBytes))
                );
              }
              node.usedBytes = newSize;
            }
          },
          node_ops: {
            getattr(node) {
              var attr = {};
              attr.dev = FS.isChrdev(node.mode) ? node.id : 1;
              attr.ino = node.id;
              attr.mode = node.mode;
              attr.nlink = 1;
              attr.uid = 0;
              attr.gid = 0;
              attr.rdev = node.rdev;
              if (FS.isDir(node.mode)) {
                attr.size = 4096;
              } else if (FS.isFile(node.mode)) {
                attr.size = node.usedBytes;
              } else if (FS.isLink(node.mode)) {
                attr.size = node.link.length;
              } else {
                attr.size = 0;
              }
              attr.atime = new Date(node.timestamp);
              attr.mtime = new Date(node.timestamp);
              attr.ctime = new Date(node.timestamp);
              attr.blksize = 4096;
              attr.blocks = Math.ceil(attr.size / attr.blksize);
              return attr;
            },
            setattr(node, attr) {
              if (attr.mode !== void 0) {
                node.mode = attr.mode;
              }
              if (attr.timestamp !== void 0) {
                node.timestamp = attr.timestamp;
              }
              if (attr.size !== void 0) {
                MEMFS.resizeFileStorage(node, attr.size);
              }
            },
            lookup(parent, name) {
              throw FS.genericErrors[44];
            },
            mknod(parent, name, mode, dev) {
              return MEMFS.createNode(parent, name, mode, dev);
            },
            rename(old_node, new_dir, new_name) {
              if (FS.isDir(old_node.mode)) {
                var new_node;
                try {
                  new_node = FS.lookupNode(new_dir, new_name);
                } catch (e) {
                }
                if (new_node) {
                  for (var i in new_node.contents) {
                    throw new FS.ErrnoError(55);
                  }
                }
              }
              delete old_node.parent.contents[old_node.name];
              old_node.parent.timestamp = Date.now();
              old_node.name = new_name;
              new_dir.contents[new_name] = old_node;
              new_dir.timestamp = old_node.parent.timestamp;
            },
            unlink(parent, name) {
              delete parent.contents[name];
              parent.timestamp = Date.now();
            },
            rmdir(parent, name) {
              var node = FS.lookupNode(parent, name);
              for (var i in node.contents) {
                throw new FS.ErrnoError(55);
              }
              delete parent.contents[name];
              parent.timestamp = Date.now();
            },
            readdir(node) {
              var entries = [".", ".."];
              for (var key of Object.keys(node.contents)) {
                entries.push(key);
              }
              return entries;
            },
            symlink(parent, newname, oldpath) {
              var node = MEMFS.createNode(
                parent,
                newname,
                511 | 40960,
                0
              );
              node.link = oldpath;
              return node;
            },
            readlink(node) {
              if (!FS.isLink(node.mode)) {
                throw new FS.ErrnoError(28);
              }
              return node.link;
            }
          },
          stream_ops: {
            read(stream, buffer, offset, length, position) {
              var contents = stream.node.contents;
              if (position >= stream.node.usedBytes) return 0;
              var size = Math.min(stream.node.usedBytes - position, length);
              assert(size >= 0);
              if (size > 8 && contents.subarray) {
                buffer.set(contents.subarray(position, position + size), offset);
              } else {
                for (var i = 0; i < size; i++)
                  buffer[offset + i] = contents[position + i];
              }
              return size;
            },
            write(stream, buffer, offset, length, position, canOwn) {
              assert(!(buffer instanceof ArrayBuffer));
              if (buffer.buffer === HEAP8.buffer) {
                canOwn = false;
              }
              if (!length) return 0;
              var node = stream.node;
              node.timestamp = Date.now();
              if (buffer.subarray && (!node.contents || node.contents.subarray)) {
                if (canOwn) {
                  assert(
                    position === 0,
                    "canOwn must imply no weird position inside the file"
                  );
                  node.contents = buffer.subarray(offset, offset + length);
                  node.usedBytes = length;
                  return length;
                } else if (node.usedBytes === 0 && position === 0) {
                  node.contents = buffer.slice(offset, offset + length);
                  node.usedBytes = length;
                  return length;
                } else if (position + length <= node.usedBytes) {
                  node.contents.set(
                    buffer.subarray(offset, offset + length),
                    position
                  );
                  return length;
                }
              }
              MEMFS.expandFileStorage(node, position + length);
              if (node.contents.subarray && buffer.subarray) {
                node.contents.set(
                  buffer.subarray(offset, offset + length),
                  position
                );
              } else {
                for (var i = 0; i < length; i++) {
                  node.contents[position + i] = buffer[offset + i];
                }
              }
              node.usedBytes = Math.max(node.usedBytes, position + length);
              return length;
            },
            llseek(stream, offset, whence) {
              var position = offset;
              if (whence === 1) {
                position += stream.position;
              } else if (whence === 2) {
                if (FS.isFile(stream.node.mode)) {
                  position += stream.node.usedBytes;
                }
              }
              if (position < 0) {
                throw new FS.ErrnoError(28);
              }
              return position;
            },
            allocate(stream, offset, length) {
              MEMFS.expandFileStorage(stream.node, offset + length);
              stream.node.usedBytes = Math.max(
                stream.node.usedBytes,
                offset + length
              );
            },
            mmap(stream, length, position, prot, flags) {
              if (!FS.isFile(stream.node.mode)) {
                throw new FS.ErrnoError(43);
              }
              var ptr;
              var allocated;
              var contents = stream.node.contents;
              if (!(flags & 2) && contents && contents.buffer === HEAP8.buffer) {
                allocated = false;
                ptr = contents.byteOffset;
              } else {
                allocated = true;
                ptr = mmapAlloc(length);
                if (!ptr) {
                  throw new FS.ErrnoError(48);
                }
                if (contents) {
                  if (position > 0 || position + length < contents.length) {
                    if (contents.subarray) {
                      contents = contents.subarray(position, position + length);
                    } else {
                      contents = Array.prototype.slice.call(
                        contents,
                        position,
                        position + length
                      );
                    }
                  }
                  HEAP8.set(contents, ptr);
                }
              }
              return { ptr, allocated };
            },
            msync(stream, buffer, offset, length, mmapFlags) {
              MEMFS.stream_ops.write(stream, buffer, 0, length, offset, false);
              return 0;
            }
          }
        };
        var asyncLoad = (url, onload, onerror, noRunDep) => {
          var dep = !noRunDep ? getUniqueRunDependency(`al ${url}`) : "";
          readAsync(url).then(
            (arrayBuffer) => {
              assert(
                arrayBuffer,
                `Loading data file "${url}" failed (no arrayBuffer).`
              );
              onload(new Uint8Array(arrayBuffer));
              if (dep) removeRunDependency(dep);
            },
            (err2) => {
              if (onerror) {
                onerror();
              } else {
                throw `Loading data file "${url}" failed.`;
              }
            }
          );
          if (dep) addRunDependency(dep);
        };
        var FS_createDataFile = (parent, name, fileData, canRead, canWrite, canOwn) => {
          FS.createDataFile(parent, name, fileData, canRead, canWrite, canOwn);
        };
        var preloadPlugins = Module["preloadPlugins"] || [];
        var FS_handledByPreloadPlugin = (byteArray, fullname, finish, onerror) => {
          if (typeof Browser != "undefined") Browser.init();
          var handled = false;
          preloadPlugins.forEach((plugin) => {
            if (handled) return;
            if (plugin["canHandle"](fullname)) {
              plugin["handle"](byteArray, fullname, finish, onerror);
              handled = true;
            }
          });
          return handled;
        };
        var FS_createPreloadedFile = (parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn, preFinish) => {
          var fullname = name ? PATH_FS.resolve(PATH.join2(parent, name)) : parent;
          var dep = getUniqueRunDependency(`cp ${fullname}`);
          function processData(byteArray) {
            function finish(byteArray2) {
              preFinish?.();
              if (!dontCreateFile) {
                FS_createDataFile(
                  parent,
                  name,
                  byteArray2,
                  canRead,
                  canWrite,
                  canOwn
                );
              }
              onload?.();
              removeRunDependency(dep);
            }
            if (FS_handledByPreloadPlugin(byteArray, fullname, finish, () => {
              onerror?.();
              removeRunDependency(dep);
            })) {
              return;
            }
            finish(byteArray);
          }
          addRunDependency(dep);
          if (typeof url == "string") {
            asyncLoad(url, processData, onerror);
          } else {
            processData(url);
          }
        };
        var FS_modeStringToFlags = (str) => {
          var flagModes = {
            r: 0,
            "r+": 2,
            w: 512 | 64 | 1,
            "w+": 512 | 64 | 2,
            a: 1024 | 64 | 1,
            "a+": 1024 | 64 | 2
          };
          var flags = flagModes[str];
          if (typeof flags == "undefined") {
            throw new Error(`Unknown file open mode: ${str}`);
          }
          return flags;
        };
        var FS_getMode = (canRead, canWrite) => {
          var mode = 0;
          if (canRead) mode |= 292 | 73;
          if (canWrite) mode |= 146;
          return mode;
        };
        var strError = (errno) => {
          return UTF8ToString(_strerror(errno));
        };
        var ERRNO_CODES = {
          EPERM: 63,
          ENOENT: 44,
          ESRCH: 71,
          EINTR: 27,
          EIO: 29,
          ENXIO: 60,
          E2BIG: 1,
          ENOEXEC: 45,
          EBADF: 8,
          ECHILD: 12,
          EAGAIN: 6,
          EWOULDBLOCK: 6,
          ENOMEM: 48,
          EACCES: 2,
          EFAULT: 21,
          ENOTBLK: 105,
          EBUSY: 10,
          EEXIST: 20,
          EXDEV: 75,
          ENODEV: 43,
          ENOTDIR: 54,
          EISDIR: 31,
          EINVAL: 28,
          ENFILE: 41,
          EMFILE: 33,
          ENOTTY: 59,
          ETXTBSY: 74,
          EFBIG: 22,
          ENOSPC: 51,
          ESPIPE: 70,
          EROFS: 69,
          EMLINK: 34,
          EPIPE: 64,
          EDOM: 18,
          ERANGE: 68,
          ENOMSG: 49,
          EIDRM: 24,
          ECHRNG: 106,
          EL2NSYNC: 156,
          EL3HLT: 107,
          EL3RST: 108,
          ELNRNG: 109,
          EUNATCH: 110,
          ENOCSI: 111,
          EL2HLT: 112,
          EDEADLK: 16,
          ENOLCK: 46,
          EBADE: 113,
          EBADR: 114,
          EXFULL: 115,
          ENOANO: 104,
          EBADRQC: 103,
          EBADSLT: 102,
          EDEADLOCK: 16,
          EBFONT: 101,
          ENOSTR: 100,
          ENODATA: 116,
          ETIME: 117,
          ENOSR: 118,
          ENONET: 119,
          ENOPKG: 120,
          EREMOTE: 121,
          ENOLINK: 47,
          EADV: 122,
          ESRMNT: 123,
          ECOMM: 124,
          EPROTO: 65,
          EMULTIHOP: 36,
          EDOTDOT: 125,
          EBADMSG: 9,
          ENOTUNIQ: 126,
          EBADFD: 127,
          EREMCHG: 128,
          ELIBACC: 129,
          ELIBBAD: 130,
          ELIBSCN: 131,
          ELIBMAX: 132,
          ELIBEXEC: 133,
          ENOSYS: 52,
          ENOTEMPTY: 55,
          ENAMETOOLONG: 37,
          ELOOP: 32,
          EOPNOTSUPP: 138,
          EPFNOSUPPORT: 139,
          ECONNRESET: 15,
          ENOBUFS: 42,
          EAFNOSUPPORT: 5,
          EPROTOTYPE: 67,
          ENOTSOCK: 57,
          ENOPROTOOPT: 50,
          ESHUTDOWN: 140,
          ECONNREFUSED: 14,
          EADDRINUSE: 3,
          ECONNABORTED: 13,
          ENETUNREACH: 40,
          ENETDOWN: 38,
          ETIMEDOUT: 73,
          EHOSTDOWN: 142,
          EHOSTUNREACH: 23,
          EINPROGRESS: 26,
          EALREADY: 7,
          EDESTADDRREQ: 17,
          EMSGSIZE: 35,
          EPROTONOSUPPORT: 66,
          ESOCKTNOSUPPORT: 137,
          EADDRNOTAVAIL: 4,
          ENETRESET: 39,
          EISCONN: 30,
          ENOTCONN: 53,
          ETOOMANYREFS: 141,
          EUSERS: 136,
          EDQUOT: 19,
          ESTALE: 72,
          ENOTSUP: 138,
          ENOMEDIUM: 148,
          EILSEQ: 25,
          EOVERFLOW: 61,
          ECANCELED: 11,
          ENOTRECOVERABLE: 56,
          EOWNERDEAD: 62,
          ESTRPIPE: 135
        };
        var FS = {
          root: null,
          mounts: [],
          devices: {},
          streams: [],
          nextInode: 1,
          nameTable: null,
          currentPath: "/",
          initialized: false,
          ignorePermissions: true,
          ErrnoError: class extends Error {
            // We set the `name` property to be able to identify `FS.ErrnoError`
            // - the `name` is a standard ECMA-262 property of error objects. Kind of good to have it anyway.
            // - when using PROXYFS, an error can come from an underlying FS
            // as different FS objects have their own FS.ErrnoError each,
            // the test `err instanceof FS.ErrnoError` won't detect an error coming from another filesystem, causing bugs.
            // we'll use the reliable test `err.name == "ErrnoError"` instead
            constructor(errno) {
              super(runtimeInitialized ? strError(errno) : "");
              this.name = "ErrnoError";
              this.errno = errno;
              for (var key in ERRNO_CODES) {
                if (ERRNO_CODES[key] === errno) {
                  this.code = key;
                  break;
                }
              }
            }
          },
          genericErrors: {},
          filesystems: null,
          syncFSRequests: 0,
          readFiles: {},
          FSStream: class {
            constructor() {
              this.shared = {};
            }
            get object() {
              return this.node;
            }
            set object(val) {
              this.node = val;
            }
            get isRead() {
              return (this.flags & 2097155) !== 1;
            }
            get isWrite() {
              return (this.flags & 2097155) !== 0;
            }
            get isAppend() {
              return this.flags & 1024;
            }
            get flags() {
              return this.shared.flags;
            }
            set flags(val) {
              this.shared.flags = val;
            }
            get position() {
              return this.shared.position;
            }
            set position(val) {
              this.shared.position = val;
            }
          },
          FSNode: class {
            constructor(parent, name, mode, rdev) {
              if (!parent) {
                parent = this;
              }
              this.parent = parent;
              this.mount = parent.mount;
              this.mounted = null;
              this.id = FS.nextInode++;
              this.name = name;
              this.mode = mode;
              this.node_ops = {};
              this.stream_ops = {};
              this.rdev = rdev;
              this.readMode = 292 | 73;
              this.writeMode = 146;
            }
            get read() {
              return (this.mode & this.readMode) === this.readMode;
            }
            set read(val) {
              val ? this.mode |= this.readMode : this.mode &= ~this.readMode;
            }
            get write() {
              return (this.mode & this.writeMode) === this.writeMode;
            }
            set write(val) {
              val ? this.mode |= this.writeMode : this.mode &= ~this.writeMode;
            }
            get isFolder() {
              return FS.isDir(this.mode);
            }
            get isDevice() {
              return FS.isChrdev(this.mode);
            }
          },
          lookupPath(path, opts = {}) {
            path = PATH_FS.resolve(path);
            if (!path) return { path: "", node: null };
            var defaults = {
              follow_mount: true,
              recurse_count: 0
            };
            opts = Object.assign(defaults, opts);
            if (opts.recurse_count > 8) {
              throw new FS.ErrnoError(32);
            }
            var parts = path.split("/").filter((p) => !!p);
            var current = FS.root;
            var current_path = "/";
            for (var i = 0; i < parts.length; i++) {
              var islast = i === parts.length - 1;
              if (islast && opts.parent) {
                break;
              }
              current = FS.lookupNode(current, parts[i]);
              current_path = PATH.join2(current_path, parts[i]);
              if (FS.isMountpoint(current)) {
                if (!islast || islast && opts.follow_mount) {
                  current = current.mounted.root;
                }
              }
              if (!islast || opts.follow) {
                var count = 0;
                while (FS.isLink(current.mode)) {
                  var link = FS.readlink(current_path);
                  current_path = PATH_FS.resolve(PATH.dirname(current_path), link);
                  var lookup = FS.lookupPath(current_path, {
                    recurse_count: opts.recurse_count + 1
                  });
                  current = lookup.node;
                  if (count++ > 40) {
                    throw new FS.ErrnoError(32);
                  }
                }
              }
            }
            return { path: current_path, node: current };
          },
          getPath(node) {
            var path;
            while (true) {
              if (FS.isRoot(node)) {
                var mount = node.mount.mountpoint;
                if (!path) return mount;
                return mount[mount.length - 1] !== "/" ? `${mount}/${path}` : mount + path;
              }
              path = path ? `${node.name}/${path}` : node.name;
              node = node.parent;
            }
          },
          hashName(parentid, name) {
            var hash = 0;
            for (var i = 0; i < name.length; i++) {
              hash = (hash << 5) - hash + name.charCodeAt(i) | 0;
            }
            return (parentid + hash >>> 0) % FS.nameTable.length;
          },
          hashAddNode(node) {
            var hash = FS.hashName(node.parent.id, node.name);
            node.name_next = FS.nameTable[hash];
            FS.nameTable[hash] = node;
          },
          hashRemoveNode(node) {
            var hash = FS.hashName(node.parent.id, node.name);
            if (FS.nameTable[hash] === node) {
              FS.nameTable[hash] = node.name_next;
            } else {
              var current = FS.nameTable[hash];
              while (current) {
                if (current.name_next === node) {
                  current.name_next = node.name_next;
                  break;
                }
                current = current.name_next;
              }
            }
          },
          lookupNode(parent, name) {
            var errCode = FS.mayLookup(parent);
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            var hash = FS.hashName(parent.id, name);
            for (var node = FS.nameTable[hash]; node; node = node.name_next) {
              var nodeName = node.name;
              if (node.parent.id === parent.id && nodeName === name) {
                return node;
              }
            }
            return FS.lookup(parent, name);
          },
          createNode(parent, name, mode, rdev) {
            assert(typeof parent == "object");
            var node = new FS.FSNode(parent, name, mode, rdev);
            FS.hashAddNode(node);
            return node;
          },
          destroyNode(node) {
            FS.hashRemoveNode(node);
          },
          isRoot(node) {
            return node === node.parent;
          },
          isMountpoint(node) {
            return !!node.mounted;
          },
          isFile(mode) {
            return (mode & 61440) === 32768;
          },
          isDir(mode) {
            return (mode & 61440) === 16384;
          },
          isLink(mode) {
            return (mode & 61440) === 40960;
          },
          isChrdev(mode) {
            return (mode & 61440) === 8192;
          },
          isBlkdev(mode) {
            return (mode & 61440) === 24576;
          },
          isFIFO(mode) {
            return (mode & 61440) === 4096;
          },
          isSocket(mode) {
            return (mode & 49152) === 49152;
          },
          flagsToPermissionString(flag) {
            var perms = ["r", "w", "rw"][flag & 3];
            if (flag & 512) {
              perms += "w";
            }
            return perms;
          },
          nodePermissions(node, perms) {
            if (FS.ignorePermissions) {
              return 0;
            }
            if (perms.includes("r") && !(node.mode & 292)) {
              return 2;
            } else if (perms.includes("w") && !(node.mode & 146)) {
              return 2;
            } else if (perms.includes("x") && !(node.mode & 73)) {
              return 2;
            }
            return 0;
          },
          mayLookup(dir) {
            if (!FS.isDir(dir.mode)) return 54;
            var errCode = FS.nodePermissions(dir, "x");
            if (errCode) return errCode;
            if (!dir.node_ops.lookup) return 2;
            return 0;
          },
          mayCreate(dir, name) {
            try {
              var node = FS.lookupNode(dir, name);
              return 20;
            } catch (e) {
            }
            return FS.nodePermissions(dir, "wx");
          },
          mayDelete(dir, name, isdir) {
            var node;
            try {
              node = FS.lookupNode(dir, name);
            } catch (e) {
              return e.errno;
            }
            var errCode = FS.nodePermissions(dir, "wx");
            if (errCode) {
              return errCode;
            }
            if (isdir) {
              if (!FS.isDir(node.mode)) {
                return 54;
              }
              if (FS.isRoot(node) || FS.getPath(node) === FS.cwd()) {
                return 10;
              }
            } else {
              if (FS.isDir(node.mode)) {
                return 31;
              }
            }
            return 0;
          },
          mayOpen(node, flags) {
            if (!node) {
              return 44;
            }
            if (FS.isLink(node.mode)) {
              return 32;
            } else if (FS.isDir(node.mode)) {
              if (FS.flagsToPermissionString(flags) !== "r" || // opening for write
              flags & 512) {
                return 31;
              }
            }
            return FS.nodePermissions(node, FS.flagsToPermissionString(flags));
          },
          MAX_OPEN_FDS: 4096,
          nextfd() {
            for (var fd = 0; fd <= FS.MAX_OPEN_FDS; fd++) {
              if (!FS.streams[fd]) {
                return fd;
              }
            }
            throw new FS.ErrnoError(33);
          },
          getStreamChecked(fd) {
            var stream = FS.getStream(fd);
            if (!stream) {
              throw new FS.ErrnoError(8);
            }
            return stream;
          },
          getStream: (fd) => FS.streams[fd],
          createStream(stream, fd = -1) {
            assert(fd >= -1);
            stream = Object.assign(new FS.FSStream(), stream);
            if (fd == -1) {
              fd = FS.nextfd();
            }
            stream.fd = fd;
            FS.streams[fd] = stream;
            return stream;
          },
          closeStream(fd) {
            FS.streams[fd] = null;
          },
          dupStream(origStream, fd = -1) {
            var stream = FS.createStream(origStream, fd);
            stream.stream_ops?.dup?.(stream);
            return stream;
          },
          chrdev_stream_ops: {
            open(stream) {
              var device = FS.getDevice(stream.node.rdev);
              stream.stream_ops = device.stream_ops;
              stream.stream_ops.open?.(stream);
            },
            llseek() {
              throw new FS.ErrnoError(70);
            }
          },
          major: (dev) => dev >> 8,
          minor: (dev) => dev & 255,
          makedev: (ma, mi) => ma << 8 | mi,
          registerDevice(dev, ops) {
            FS.devices[dev] = { stream_ops: ops };
          },
          getDevice: (dev) => FS.devices[dev],
          getMounts(mount) {
            var mounts = [];
            var check = [mount];
            while (check.length) {
              var m = check.pop();
              mounts.push(m);
              check.push(...m.mounts);
            }
            return mounts;
          },
          syncfs(populate, callback) {
            if (typeof populate == "function") {
              callback = populate;
              populate = false;
            }
            FS.syncFSRequests++;
            if (FS.syncFSRequests > 1) {
              err(
                `warning: ${FS.syncFSRequests} FS.syncfs operations in flight at once, probably just doing extra work`
              );
            }
            var mounts = FS.getMounts(FS.root.mount);
            var completed = 0;
            function doCallback(errCode) {
              assert(FS.syncFSRequests > 0);
              FS.syncFSRequests--;
              return callback(errCode);
            }
            function done(errCode) {
              if (errCode) {
                if (!done.errored) {
                  done.errored = true;
                  return doCallback(errCode);
                }
                return;
              }
              if (++completed >= mounts.length) {
                doCallback(null);
              }
            }
            mounts.forEach((mount) => {
              if (!mount.type.syncfs) {
                return done(null);
              }
              mount.type.syncfs(mount, populate, done);
            });
          },
          mount(type, opts, mountpoint) {
            if (typeof type == "string") {
              throw type;
            }
            var root = mountpoint === "/";
            var pseudo = !mountpoint;
            var node;
            if (root && FS.root) {
              throw new FS.ErrnoError(10);
            } else if (!root && !pseudo) {
              var lookup = FS.lookupPath(mountpoint, { follow_mount: false });
              mountpoint = lookup.path;
              node = lookup.node;
              if (FS.isMountpoint(node)) {
                throw new FS.ErrnoError(10);
              }
              if (!FS.isDir(node.mode)) {
                throw new FS.ErrnoError(54);
              }
            }
            var mount = {
              type,
              opts,
              mountpoint,
              mounts: []
            };
            var mountRoot = type.mount(mount);
            mountRoot.mount = mount;
            mount.root = mountRoot;
            if (root) {
              FS.root = mountRoot;
            } else if (node) {
              node.mounted = mount;
              if (node.mount) {
                node.mount.mounts.push(mount);
              }
            }
            return mountRoot;
          },
          unmount(mountpoint) {
            var lookup = FS.lookupPath(mountpoint, { follow_mount: false });
            if (!FS.isMountpoint(lookup.node)) {
              throw new FS.ErrnoError(28);
            }
            var node = lookup.node;
            var mount = node.mounted;
            var mounts = FS.getMounts(mount);
            Object.keys(FS.nameTable).forEach((hash) => {
              var current = FS.nameTable[hash];
              while (current) {
                var next = current.name_next;
                if (mounts.includes(current.mount)) {
                  FS.destroyNode(current);
                }
                current = next;
              }
            });
            node.mounted = null;
            var idx = node.mount.mounts.indexOf(mount);
            assert(idx !== -1);
            node.mount.mounts.splice(idx, 1);
          },
          lookup(parent, name) {
            return parent.node_ops.lookup(parent, name);
          },
          mknod(path, mode, dev) {
            var lookup = FS.lookupPath(path, { parent: true });
            var parent = lookup.node;
            var name = PATH.basename(path);
            if (!name || name === "." || name === "..") {
              throw new FS.ErrnoError(28);
            }
            var errCode = FS.mayCreate(parent, name);
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            if (!parent.node_ops.mknod) {
              throw new FS.ErrnoError(63);
            }
            return parent.node_ops.mknod(parent, name, mode, dev);
          },
          create(path, mode) {
            mode = mode !== void 0 ? mode : 438;
            mode &= 4095;
            mode |= 32768;
            return FS.mknod(path, mode, 0);
          },
          mkdir(path, mode) {
            mode = mode !== void 0 ? mode : 511;
            mode &= 511 | 512;
            mode |= 16384;
            return FS.mknod(path, mode, 0);
          },
          mkdirTree(path, mode) {
            var dirs = path.split("/");
            var d = "";
            for (var i = 0; i < dirs.length; ++i) {
              if (!dirs[i]) continue;
              d += "/" + dirs[i];
              try {
                FS.mkdir(d, mode);
              } catch (e) {
                if (e.errno != 20) throw e;
              }
            }
          },
          mkdev(path, mode, dev) {
            if (typeof dev == "undefined") {
              dev = mode;
              mode = 438;
            }
            mode |= 8192;
            return FS.mknod(path, mode, dev);
          },
          symlink(oldpath, newpath) {
            if (!PATH_FS.resolve(oldpath)) {
              throw new FS.ErrnoError(44);
            }
            var lookup = FS.lookupPath(newpath, { parent: true });
            var parent = lookup.node;
            if (!parent) {
              throw new FS.ErrnoError(44);
            }
            var newname = PATH.basename(newpath);
            var errCode = FS.mayCreate(parent, newname);
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            if (!parent.node_ops.symlink) {
              throw new FS.ErrnoError(63);
            }
            return parent.node_ops.symlink(parent, newname, oldpath);
          },
          rename(old_path, new_path) {
            var old_dirname = PATH.dirname(old_path);
            var new_dirname = PATH.dirname(new_path);
            var old_name = PATH.basename(old_path);
            var new_name = PATH.basename(new_path);
            var lookup, old_dir, new_dir;
            lookup = FS.lookupPath(old_path, { parent: true });
            old_dir = lookup.node;
            lookup = FS.lookupPath(new_path, { parent: true });
            new_dir = lookup.node;
            if (!old_dir || !new_dir) throw new FS.ErrnoError(44);
            if (old_dir.mount !== new_dir.mount) {
              throw new FS.ErrnoError(75);
            }
            var old_node = FS.lookupNode(old_dir, old_name);
            var relative = PATH_FS.relative(old_path, new_dirname);
            if (relative.charAt(0) !== ".") {
              throw new FS.ErrnoError(28);
            }
            relative = PATH_FS.relative(new_path, old_dirname);
            if (relative.charAt(0) !== ".") {
              throw new FS.ErrnoError(55);
            }
            var new_node;
            try {
              new_node = FS.lookupNode(new_dir, new_name);
            } catch (e) {
            }
            if (old_node === new_node) {
              return;
            }
            var isdir = FS.isDir(old_node.mode);
            var errCode = FS.mayDelete(old_dir, old_name, isdir);
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            errCode = new_node ? FS.mayDelete(new_dir, new_name, isdir) : FS.mayCreate(new_dir, new_name);
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            if (!old_dir.node_ops.rename) {
              throw new FS.ErrnoError(63);
            }
            if (FS.isMountpoint(old_node) || new_node && FS.isMountpoint(new_node)) {
              throw new FS.ErrnoError(10);
            }
            if (new_dir !== old_dir) {
              errCode = FS.nodePermissions(old_dir, "w");
              if (errCode) {
                throw new FS.ErrnoError(errCode);
              }
            }
            FS.hashRemoveNode(old_node);
            try {
              old_dir.node_ops.rename(old_node, new_dir, new_name);
              old_node.parent = new_dir;
            } catch (e) {
              throw e;
            } finally {
              FS.hashAddNode(old_node);
            }
          },
          rmdir(path) {
            var lookup = FS.lookupPath(path, { parent: true });
            var parent = lookup.node;
            var name = PATH.basename(path);
            var node = FS.lookupNode(parent, name);
            var errCode = FS.mayDelete(parent, name, true);
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            if (!parent.node_ops.rmdir) {
              throw new FS.ErrnoError(63);
            }
            if (FS.isMountpoint(node)) {
              throw new FS.ErrnoError(10);
            }
            parent.node_ops.rmdir(parent, name);
            FS.destroyNode(node);
          },
          readdir(path) {
            var lookup = FS.lookupPath(path, { follow: true });
            var node = lookup.node;
            if (!node.node_ops.readdir) {
              throw new FS.ErrnoError(54);
            }
            return node.node_ops.readdir(node);
          },
          unlink(path) {
            var lookup = FS.lookupPath(path, { parent: true });
            var parent = lookup.node;
            if (!parent) {
              throw new FS.ErrnoError(44);
            }
            var name = PATH.basename(path);
            var node = FS.lookupNode(parent, name);
            var errCode = FS.mayDelete(parent, name, false);
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            if (!parent.node_ops.unlink) {
              throw new FS.ErrnoError(63);
            }
            if (FS.isMountpoint(node)) {
              throw new FS.ErrnoError(10);
            }
            parent.node_ops.unlink(parent, name);
            FS.destroyNode(node);
          },
          readlink(path) {
            var lookup = FS.lookupPath(path);
            var link = lookup.node;
            if (!link) {
              throw new FS.ErrnoError(44);
            }
            if (!link.node_ops.readlink) {
              throw new FS.ErrnoError(28);
            }
            return PATH_FS.resolve(
              FS.getPath(link.parent),
              link.node_ops.readlink(link)
            );
          },
          stat(path, dontFollow) {
            var lookup = FS.lookupPath(path, { follow: !dontFollow });
            var node = lookup.node;
            if (!node) {
              throw new FS.ErrnoError(44);
            }
            if (!node.node_ops.getattr) {
              throw new FS.ErrnoError(63);
            }
            return node.node_ops.getattr(node);
          },
          lstat(path) {
            return FS.stat(path, true);
          },
          chmod(path, mode, dontFollow) {
            var node;
            if (typeof path == "string") {
              var lookup = FS.lookupPath(path, { follow: !dontFollow });
              node = lookup.node;
            } else {
              node = path;
            }
            if (!node.node_ops.setattr) {
              throw new FS.ErrnoError(63);
            }
            node.node_ops.setattr(node, {
              mode: mode & 4095 | node.mode & ~4095,
              timestamp: Date.now()
            });
          },
          lchmod(path, mode) {
            FS.chmod(path, mode, true);
          },
          fchmod(fd, mode) {
            var stream = FS.getStreamChecked(fd);
            FS.chmod(stream.node, mode);
          },
          chown(path, uid, gid, dontFollow) {
            var node;
            if (typeof path == "string") {
              var lookup = FS.lookupPath(path, { follow: !dontFollow });
              node = lookup.node;
            } else {
              node = path;
            }
            if (!node.node_ops.setattr) {
              throw new FS.ErrnoError(63);
            }
            node.node_ops.setattr(node, {
              timestamp: Date.now()
              // we ignore the uid / gid for now
            });
          },
          lchown(path, uid, gid) {
            FS.chown(path, uid, gid, true);
          },
          fchown(fd, uid, gid) {
            var stream = FS.getStreamChecked(fd);
            FS.chown(stream.node, uid, gid);
          },
          truncate(path, len) {
            if (len < 0) {
              throw new FS.ErrnoError(28);
            }
            var node;
            if (typeof path == "string") {
              var lookup = FS.lookupPath(path, { follow: true });
              node = lookup.node;
            } else {
              node = path;
            }
            if (!node.node_ops.setattr) {
              throw new FS.ErrnoError(63);
            }
            if (FS.isDir(node.mode)) {
              throw new FS.ErrnoError(31);
            }
            if (!FS.isFile(node.mode)) {
              throw new FS.ErrnoError(28);
            }
            var errCode = FS.nodePermissions(node, "w");
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            node.node_ops.setattr(node, {
              size: len,
              timestamp: Date.now()
            });
          },
          ftruncate(fd, len) {
            var stream = FS.getStreamChecked(fd);
            if ((stream.flags & 2097155) === 0) {
              throw new FS.ErrnoError(28);
            }
            FS.truncate(stream.node, len);
          },
          utime(path, atime, mtime) {
            var lookup = FS.lookupPath(path, { follow: true });
            var node = lookup.node;
            node.node_ops.setattr(node, {
              timestamp: Math.max(atime, mtime)
            });
          },
          open(path, flags, mode) {
            if (path === "") {
              throw new FS.ErrnoError(44);
            }
            flags = typeof flags == "string" ? FS_modeStringToFlags(flags) : flags;
            if (flags & 64) {
              mode = typeof mode == "undefined" ? 438 : mode;
              mode = mode & 4095 | 32768;
            } else {
              mode = 0;
            }
            var node;
            if (typeof path == "object") {
              node = path;
            } else {
              path = PATH.normalize(path);
              try {
                var lookup = FS.lookupPath(path, {
                  follow: !(flags & 131072)
                });
                node = lookup.node;
              } catch (e) {
              }
            }
            var created = false;
            if (flags & 64) {
              if (node) {
                if (flags & 128) {
                  throw new FS.ErrnoError(20);
                }
              } else {
                node = FS.mknod(path, mode, 0);
                created = true;
              }
            }
            if (!node) {
              throw new FS.ErrnoError(44);
            }
            if (FS.isChrdev(node.mode)) {
              flags &= ~512;
            }
            if (flags & 65536 && !FS.isDir(node.mode)) {
              throw new FS.ErrnoError(54);
            }
            if (!created) {
              var errCode = FS.mayOpen(node, flags);
              if (errCode) {
                throw new FS.ErrnoError(errCode);
              }
            }
            if (flags & 512 && !created) {
              FS.truncate(node, 0);
            }
            flags &= ~(128 | 512 | 131072);
            var stream = FS.createStream({
              node,
              path: FS.getPath(node),
              // we want the absolute path to the node
              flags,
              seekable: true,
              position: 0,
              stream_ops: node.stream_ops,
              // used by the file family libc calls (fopen, fwrite, ferror, etc.)
              ungotten: [],
              error: false
            });
            if (stream.stream_ops.open) {
              stream.stream_ops.open(stream);
            }
            if (Module["logReadFiles"] && !(flags & 1)) {
              if (!(path in FS.readFiles)) {
                FS.readFiles[path] = 1;
              }
            }
            return stream;
          },
          close(stream) {
            if (FS.isClosed(stream)) {
              throw new FS.ErrnoError(8);
            }
            if (stream.getdents) stream.getdents = null;
            try {
              if (stream.stream_ops.close) {
                stream.stream_ops.close(stream);
              }
            } catch (e) {
              throw e;
            } finally {
              FS.closeStream(stream.fd);
            }
            stream.fd = null;
          },
          isClosed(stream) {
            return stream.fd === null;
          },
          llseek(stream, offset, whence) {
            if (FS.isClosed(stream)) {
              throw new FS.ErrnoError(8);
            }
            if (!stream.seekable || !stream.stream_ops.llseek) {
              throw new FS.ErrnoError(70);
            }
            if (whence != 0 && whence != 1 && whence != 2) {
              throw new FS.ErrnoError(28);
            }
            stream.position = stream.stream_ops.llseek(stream, offset, whence);
            stream.ungotten = [];
            return stream.position;
          },
          read(stream, buffer, offset, length, position) {
            assert(offset >= 0);
            if (length < 0 || position < 0) {
              throw new FS.ErrnoError(28);
            }
            if (FS.isClosed(stream)) {
              throw new FS.ErrnoError(8);
            }
            if ((stream.flags & 2097155) === 1) {
              throw new FS.ErrnoError(8);
            }
            if (FS.isDir(stream.node.mode)) {
              throw new FS.ErrnoError(31);
            }
            if (!stream.stream_ops.read) {
              throw new FS.ErrnoError(28);
            }
            var seeking = typeof position != "undefined";
            if (!seeking) {
              position = stream.position;
            } else if (!stream.seekable) {
              throw new FS.ErrnoError(70);
            }
            var bytesRead = stream.stream_ops.read(
              stream,
              buffer,
              offset,
              length,
              position
            );
            if (!seeking) stream.position += bytesRead;
            return bytesRead;
          },
          write(stream, buffer, offset, length, position, canOwn) {
            assert(offset >= 0);
            if (length < 0 || position < 0) {
              throw new FS.ErrnoError(28);
            }
            if (FS.isClosed(stream)) {
              throw new FS.ErrnoError(8);
            }
            if ((stream.flags & 2097155) === 0) {
              throw new FS.ErrnoError(8);
            }
            if (FS.isDir(stream.node.mode)) {
              throw new FS.ErrnoError(31);
            }
            if (!stream.stream_ops.write) {
              throw new FS.ErrnoError(28);
            }
            if (stream.seekable && stream.flags & 1024) {
              FS.llseek(stream, 0, 2);
            }
            var seeking = typeof position != "undefined";
            if (!seeking) {
              position = stream.position;
            } else if (!stream.seekable) {
              throw new FS.ErrnoError(70);
            }
            var bytesWritten = stream.stream_ops.write(
              stream,
              buffer,
              offset,
              length,
              position,
              canOwn
            );
            if (!seeking) stream.position += bytesWritten;
            return bytesWritten;
          },
          allocate(stream, offset, length) {
            if (FS.isClosed(stream)) {
              throw new FS.ErrnoError(8);
            }
            if (offset < 0 || length <= 0) {
              throw new FS.ErrnoError(28);
            }
            if ((stream.flags & 2097155) === 0) {
              throw new FS.ErrnoError(8);
            }
            if (!FS.isFile(stream.node.mode) && !FS.isDir(stream.node.mode)) {
              throw new FS.ErrnoError(43);
            }
            if (!stream.stream_ops.allocate) {
              throw new FS.ErrnoError(138);
            }
            stream.stream_ops.allocate(stream, offset, length);
          },
          mmap(stream, length, position, prot, flags) {
            if ((prot & 2) !== 0 && (flags & 2) === 0 && (stream.flags & 2097155) !== 2) {
              throw new FS.ErrnoError(2);
            }
            if ((stream.flags & 2097155) === 1) {
              throw new FS.ErrnoError(2);
            }
            if (!stream.stream_ops.mmap) {
              throw new FS.ErrnoError(43);
            }
            if (!length) {
              throw new FS.ErrnoError(28);
            }
            return stream.stream_ops.mmap(stream, length, position, prot, flags);
          },
          msync(stream, buffer, offset, length, mmapFlags) {
            assert(offset >= 0);
            if (!stream.stream_ops.msync) {
              return 0;
            }
            return stream.stream_ops.msync(
              stream,
              buffer,
              offset,
              length,
              mmapFlags
            );
          },
          ioctl(stream, cmd, arg) {
            if (!stream.stream_ops.ioctl) {
              throw new FS.ErrnoError(59);
            }
            return stream.stream_ops.ioctl(stream, cmd, arg);
          },
          readFile(path, opts = {}) {
            opts.flags = opts.flags || 0;
            opts.encoding = opts.encoding || "binary";
            if (opts.encoding !== "utf8" && opts.encoding !== "binary") {
              throw new Error(`Invalid encoding type "${opts.encoding}"`);
            }
            var ret;
            var stream = FS.open(path, opts.flags);
            var stat = FS.stat(path);
            var length = stat.size;
            var buf = new Uint8Array(length);
            FS.read(stream, buf, 0, length, 0);
            if (opts.encoding === "utf8") {
              ret = UTF8ArrayToString(buf);
            } else if (opts.encoding === "binary") {
              ret = buf;
            }
            FS.close(stream);
            return ret;
          },
          writeFile(path, data, opts = {}) {
            opts.flags = opts.flags || 577;
            var stream = FS.open(path, opts.flags, opts.mode);
            if (typeof data == "string") {
              var buf = new Uint8Array(lengthBytesUTF8(data) + 1);
              var actualNumBytes = stringToUTF8Array(data, buf, 0, buf.length);
              FS.write(stream, buf, 0, actualNumBytes, void 0, opts.canOwn);
            } else if (ArrayBuffer.isView(data)) {
              FS.write(stream, data, 0, data.byteLength, void 0, opts.canOwn);
            } else {
              throw new Error("Unsupported data type");
            }
            FS.close(stream);
          },
          cwd: () => FS.currentPath,
          chdir(path) {
            var lookup = FS.lookupPath(path, { follow: true });
            if (lookup.node === null) {
              throw new FS.ErrnoError(44);
            }
            if (!FS.isDir(lookup.node.mode)) {
              throw new FS.ErrnoError(54);
            }
            var errCode = FS.nodePermissions(lookup.node, "x");
            if (errCode) {
              throw new FS.ErrnoError(errCode);
            }
            FS.currentPath = lookup.path;
          },
          createDefaultDirectories() {
            FS.mkdir("/tmp");
            FS.mkdir("/home");
            FS.mkdir("/home/web_user");
          },
          createDefaultDevices() {
            FS.mkdir("/dev");
            FS.registerDevice(FS.makedev(1, 3), {
              read: () => 0,
              write: (stream, buffer, offset, length, pos) => length
            });
            FS.mkdev("/dev/null", FS.makedev(1, 3));
            TTY.register(FS.makedev(5, 0), TTY.default_tty_ops);
            TTY.register(FS.makedev(6, 0), TTY.default_tty1_ops);
            FS.mkdev("/dev/tty", FS.makedev(5, 0));
            FS.mkdev("/dev/tty1", FS.makedev(6, 0));
            var randomBuffer = new Uint8Array(1024), randomLeft = 0;
            var randomByte = () => {
              if (randomLeft === 0) {
                randomLeft = randomFill(randomBuffer).byteLength;
              }
              return randomBuffer[--randomLeft];
            };
            FS.createDevice("/dev", "random", randomByte);
            FS.createDevice("/dev", "urandom", randomByte);
            FS.mkdir("/dev/shm");
            FS.mkdir("/dev/shm/tmp");
          },
          createSpecialDirectories() {
            FS.mkdir("/proc");
            var proc_self = FS.mkdir("/proc/self");
            FS.mkdir("/proc/self/fd");
            FS.mount(
              {
                mount() {
                  var node = FS.createNode(
                    proc_self,
                    "fd",
                    16384 | 511,
                    73
                  );
                  node.node_ops = {
                    lookup(parent, name) {
                      var fd = +name;
                      var stream = FS.getStreamChecked(fd);
                      var ret = {
                        parent: null,
                        mount: { mountpoint: "fake" },
                        node_ops: { readlink: () => stream.path }
                      };
                      ret.parent = ret;
                      return ret;
                    }
                  };
                  return node;
                }
              },
              {},
              "/proc/self/fd"
            );
          },
          createStandardStreams(input, output, error) {
            if (input) {
              FS.createDevice("/dev", "stdin", input);
            } else {
              FS.symlink("/dev/tty", "/dev/stdin");
            }
            if (output) {
              FS.createDevice("/dev", "stdout", null, output);
            } else {
              FS.symlink("/dev/tty", "/dev/stdout");
            }
            if (error) {
              FS.createDevice("/dev", "stderr", null, error);
            } else {
              FS.symlink("/dev/tty1", "/dev/stderr");
            }
            var stdin = FS.open("/dev/stdin", 0);
            var stdout = FS.open("/dev/stdout", 1);
            var stderr = FS.open("/dev/stderr", 1);
            assert(stdin.fd === 0, `invalid handle for stdin (${stdin.fd})`);
            assert(stdout.fd === 1, `invalid handle for stdout (${stdout.fd})`);
            assert(stderr.fd === 2, `invalid handle for stderr (${stderr.fd})`);
          },
          staticInit() {
            [44].forEach((code) => {
              FS.genericErrors[code] = new FS.ErrnoError(code);
              FS.genericErrors[code].stack = "<generic error, no stack>";
            });
            FS.nameTable = new Array(4096);
            FS.mount(MEMFS, {}, "/");
            FS.createDefaultDirectories();
            FS.createDefaultDevices();
            FS.createSpecialDirectories();
            FS.filesystems = {
              MEMFS
            };
          },
          init(input, output, error) {
            assert(
              !FS.initialized,
              "FS.init was previously called. If you want to initialize later with custom parameters, remove any earlier calls (note that one is automatically added to the generated code)"
            );
            FS.initialized = true;
            input ??= Module["stdin"];
            output ??= Module["stdout"];
            error ??= Module["stderr"];
            FS.createStandardStreams(input, output, error);
          },
          quit() {
            FS.initialized = false;
            _fflush(0);
            for (var i = 0; i < FS.streams.length; i++) {
              var stream = FS.streams[i];
              if (!stream) {
                continue;
              }
              FS.close(stream);
            }
          },
          findObject(path, dontResolveLastLink) {
            var ret = FS.analyzePath(path, dontResolveLastLink);
            if (!ret.exists) {
              return null;
            }
            return ret.object;
          },
          analyzePath(path, dontResolveLastLink) {
            try {
              var lookup = FS.lookupPath(path, { follow: !dontResolveLastLink });
              path = lookup.path;
            } catch (e) {
            }
            var ret = {
              isRoot: false,
              exists: false,
              error: 0,
              name: null,
              path: null,
              object: null,
              parentExists: false,
              parentPath: null,
              parentObject: null
            };
            try {
              var lookup = FS.lookupPath(path, { parent: true });
              ret.parentExists = true;
              ret.parentPath = lookup.path;
              ret.parentObject = lookup.node;
              ret.name = PATH.basename(path);
              lookup = FS.lookupPath(path, { follow: !dontResolveLastLink });
              ret.exists = true;
              ret.path = lookup.path;
              ret.object = lookup.node;
              ret.name = lookup.node.name;
              ret.isRoot = lookup.path === "/";
            } catch (e) {
              ret.error = e.errno;
            }
            return ret;
          },
          createPath(parent, path, canRead, canWrite) {
            parent = typeof parent == "string" ? parent : FS.getPath(parent);
            var parts = path.split("/").reverse();
            while (parts.length) {
              var part = parts.pop();
              if (!part) continue;
              var current = PATH.join2(parent, part);
              try {
                FS.mkdir(current);
              } catch (e) {
              }
              parent = current;
            }
            return current;
          },
          createFile(parent, name, properties, canRead, canWrite) {
            var path = PATH.join2(
              typeof parent == "string" ? parent : FS.getPath(parent),
              name
            );
            var mode = FS_getMode(canRead, canWrite);
            return FS.create(path, mode);
          },
          createDataFile(parent, name, data, canRead, canWrite, canOwn) {
            var path = name;
            if (parent) {
              parent = typeof parent == "string" ? parent : FS.getPath(parent);
              path = name ? PATH.join2(parent, name) : parent;
            }
            var mode = FS_getMode(canRead, canWrite);
            var node = FS.create(path, mode);
            if (data) {
              if (typeof data == "string") {
                var arr = new Array(data.length);
                for (var i = 0, len = data.length; i < len; ++i)
                  arr[i] = data.charCodeAt(i);
                data = arr;
              }
              FS.chmod(node, mode | 146);
              var stream = FS.open(node, 577);
              FS.write(stream, data, 0, data.length, 0, canOwn);
              FS.close(stream);
              FS.chmod(node, mode);
            }
          },
          createDevice(parent, name, input, output) {
            var path = PATH.join2(
              typeof parent == "string" ? parent : FS.getPath(parent),
              name
            );
            var mode = FS_getMode(!!input, !!output);
            FS.createDevice.major ??= 64;
            var dev = FS.makedev(FS.createDevice.major++, 0);
            FS.registerDevice(dev, {
              open(stream) {
                stream.seekable = false;
              },
              close(stream) {
                if (output?.buffer?.length) {
                  output(10);
                }
              },
              read(stream, buffer, offset, length, pos) {
                var bytesRead = 0;
                for (var i = 0; i < length; i++) {
                  var result;
                  try {
                    result = input();
                  } catch (e) {
                    throw new FS.ErrnoError(29);
                  }
                  if (result === void 0 && bytesRead === 0) {
                    throw new FS.ErrnoError(6);
                  }
                  if (result === null || result === void 0) break;
                  bytesRead++;
                  buffer[offset + i] = result;
                }
                if (bytesRead) {
                  stream.node.timestamp = Date.now();
                }
                return bytesRead;
              },
              write(stream, buffer, offset, length, pos) {
                for (var i = 0; i < length; i++) {
                  try {
                    output(buffer[offset + i]);
                  } catch (e) {
                    throw new FS.ErrnoError(29);
                  }
                }
                if (length) {
                  stream.node.timestamp = Date.now();
                }
                return i;
              }
            });
            return FS.mkdev(path, mode, dev);
          },
          forceLoadFile(obj) {
            if (obj.isDevice || obj.isFolder || obj.link || obj.contents)
              return true;
            if (typeof XMLHttpRequest != "undefined") {
              throw new Error(
                "Lazy loading should have been performed (contents set) in createLazyFile, but it was not. Lazy loading only works in web workers. Use --embed-file or --preload-file in emcc on the main thread."
              );
            } else {
              try {
                obj.contents = readBinary(obj.url);
                obj.usedBytes = obj.contents.length;
              } catch (e) {
                throw new FS.ErrnoError(29);
              }
            }
          },
          createLazyFile(parent, name, url, canRead, canWrite) {
            class LazyUint8Array {
              constructor() {
                this.lengthKnown = false;
                this.chunks = [];
              }
              get(idx) {
                if (idx > this.length - 1 || idx < 0) {
                  return void 0;
                }
                var chunkOffset = idx % this.chunkSize;
                var chunkNum = idx / this.chunkSize | 0;
                return this.getter(chunkNum)[chunkOffset];
              }
              setDataGetter(getter) {
                this.getter = getter;
              }
              cacheLength() {
                var xhr = new XMLHttpRequest();
                xhr.open("HEAD", url, false);
                xhr.send(null);
                if (!(xhr.status >= 200 && xhr.status < 300 || xhr.status === 304))
                  throw new Error(
                    "Couldn't load " + url + ". Status: " + xhr.status
                  );
                var datalength = Number(xhr.getResponseHeader("Content-length"));
                var header;
                var hasByteServing = (header = xhr.getResponseHeader("Accept-Ranges")) && header === "bytes";
                var usesGzip = (header = xhr.getResponseHeader("Content-Encoding")) && header === "gzip";
                var chunkSize = 1024 * 1024;
                if (!hasByteServing) chunkSize = datalength;
                var doXHR = (from, to) => {
                  if (from > to)
                    throw new Error(
                      "invalid range (" + from + ", " + to + ") or no bytes requested!"
                    );
                  if (to > datalength - 1)
                    throw new Error(
                      "only " + datalength + " bytes available! programmer error!"
                    );
                  var xhr2 = new XMLHttpRequest();
                  xhr2.open("GET", url, false);
                  if (datalength !== chunkSize)
                    xhr2.setRequestHeader("Range", "bytes=" + from + "-" + to);
                  xhr2.responseType = "arraybuffer";
                  if (xhr2.overrideMimeType) {
                    xhr2.overrideMimeType("text/plain; charset=x-user-defined");
                  }
                  xhr2.send(null);
                  if (!(xhr2.status >= 200 && xhr2.status < 300 || xhr2.status === 304))
                    throw new Error(
                      "Couldn't load " + url + ". Status: " + xhr2.status
                    );
                  if (xhr2.response !== void 0) {
                    return new Uint8Array(
                      /** @type{Array<number>} */
                      xhr2.response || []
                    );
                  }
                  return intArrayFromString(xhr2.responseText || "", true);
                };
                var lazyArray2 = this;
                lazyArray2.setDataGetter((chunkNum) => {
                  var start = chunkNum * chunkSize;
                  var end = (chunkNum + 1) * chunkSize - 1;
                  end = Math.min(end, datalength - 1);
                  if (typeof lazyArray2.chunks[chunkNum] == "undefined") {
                    lazyArray2.chunks[chunkNum] = doXHR(start, end);
                  }
                  if (typeof lazyArray2.chunks[chunkNum] == "undefined")
                    throw new Error("doXHR failed!");
                  return lazyArray2.chunks[chunkNum];
                });
                if (usesGzip || !datalength) {
                  chunkSize = datalength = 1;
                  datalength = this.getter(0).length;
                  chunkSize = datalength;
                  out(
                    "LazyFiles on gzip forces download of the whole file when length is accessed"
                  );
                }
                this._length = datalength;
                this._chunkSize = chunkSize;
                this.lengthKnown = true;
              }
              get length() {
                if (!this.lengthKnown) {
                  this.cacheLength();
                }
                return this._length;
              }
              get chunkSize() {
                if (!this.lengthKnown) {
                  this.cacheLength();
                }
                return this._chunkSize;
              }
            }
            if (typeof XMLHttpRequest != "undefined") {
              if (!ENVIRONMENT_IS_WORKER)
                throw "Cannot do synchronous binary XHRs outside webworkers in modern browsers. Use --embed-file or --preload-file in emcc";
              var lazyArray = new LazyUint8Array();
              var properties = { isDevice: false, contents: lazyArray };
            } else {
              var properties = { isDevice: false, url };
            }
            var node = FS.createFile(parent, name, properties, canRead, canWrite);
            if (properties.contents) {
              node.contents = properties.contents;
            } else if (properties.url) {
              node.contents = null;
              node.url = properties.url;
            }
            Object.defineProperties(node, {
              usedBytes: {
                get: function() {
                  return this.contents.length;
                }
              }
            });
            var stream_ops = {};
            var keys = Object.keys(node.stream_ops);
            keys.forEach((key) => {
              var fn = node.stream_ops[key];
              stream_ops[key] = (...args) => {
                FS.forceLoadFile(node);
                return fn(...args);
              };
            });
            function writeChunks(stream, buffer, offset, length, position) {
              var contents = stream.node.contents;
              if (position >= contents.length) return 0;
              var size = Math.min(contents.length - position, length);
              assert(size >= 0);
              if (contents.slice) {
                for (var i = 0; i < size; i++) {
                  buffer[offset + i] = contents[position + i];
                }
              } else {
                for (var i = 0; i < size; i++) {
                  buffer[offset + i] = contents.get(position + i);
                }
              }
              return size;
            }
            stream_ops.read = (stream, buffer, offset, length, position) => {
              FS.forceLoadFile(node);
              return writeChunks(stream, buffer, offset, length, position);
            };
            stream_ops.mmap = (stream, length, position, prot, flags) => {
              FS.forceLoadFile(node);
              var ptr = mmapAlloc(length);
              if (!ptr) {
                throw new FS.ErrnoError(48);
              }
              writeChunks(stream, HEAP8, ptr, length, position);
              return { ptr, allocated: true };
            };
            node.stream_ops = stream_ops;
            return node;
          },
          absolutePath() {
            abort("FS.absolutePath has been removed; use PATH_FS.resolve instead");
          },
          createFolder() {
            abort("FS.createFolder has been removed; use FS.mkdir instead");
          },
          createLink() {
            abort("FS.createLink has been removed; use FS.symlink instead");
          },
          joinPath() {
            abort("FS.joinPath has been removed; use PATH.join instead");
          },
          mmapAlloc() {
            abort(
              "FS.mmapAlloc has been replaced by the top level function mmapAlloc"
            );
          },
          standardizePath() {
            abort(
              "FS.standardizePath has been removed; use PATH.normalize instead"
            );
          }
        };
        var SYSCALLS = {
          DEFAULT_POLLMASK: 5,
          calculateAt(dirfd, path, allowEmpty) {
            if (PATH.isAbs(path)) {
              return path;
            }
            var dir;
            if (dirfd === -100) {
              dir = FS.cwd();
            } else {
              var dirstream = SYSCALLS.getStreamFromFD(dirfd);
              dir = dirstream.path;
            }
            if (path.length == 0) {
              if (!allowEmpty) {
                throw new FS.ErrnoError(44);
              }
              return dir;
            }
            return PATH.join2(dir, path);
          },
          doStat(func, path, buf) {
            var stat = func(path);
            HEAP32[buf >> 2] = stat.dev;
            HEAP32[buf + 4 >> 2] = stat.mode;
            HEAPU32[buf + 8 >> 2] = stat.nlink;
            HEAP32[buf + 12 >> 2] = stat.uid;
            HEAP32[buf + 16 >> 2] = stat.gid;
            HEAP32[buf + 20 >> 2] = stat.rdev;
            tempI64 = [
              stat.size >>> 0,
              (tempDouble = stat.size, +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil(
                (tempDouble - +(~~tempDouble >>> 0)) / 4294967296
              ) >>> 0 : 0)
            ], HEAP32[buf + 24 >> 2] = tempI64[0], HEAP32[buf + 28 >> 2] = tempI64[1];
            HEAP32[buf + 32 >> 2] = 4096;
            HEAP32[buf + 36 >> 2] = stat.blocks;
            var atime = stat.atime.getTime();
            var mtime = stat.mtime.getTime();
            var ctime = stat.ctime.getTime();
            tempI64 = [
              Math.floor(atime / 1e3) >>> 0,
              (tempDouble = Math.floor(atime / 1e3), +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil(
                (tempDouble - +(~~tempDouble >>> 0)) / 4294967296
              ) >>> 0 : 0)
            ], HEAP32[buf + 40 >> 2] = tempI64[0], HEAP32[buf + 44 >> 2] = tempI64[1];
            HEAPU32[buf + 48 >> 2] = atime % 1e3 * 1e3 * 1e3;
            tempI64 = [
              Math.floor(mtime / 1e3) >>> 0,
              (tempDouble = Math.floor(mtime / 1e3), +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil(
                (tempDouble - +(~~tempDouble >>> 0)) / 4294967296
              ) >>> 0 : 0)
            ], HEAP32[buf + 56 >> 2] = tempI64[0], HEAP32[buf + 60 >> 2] = tempI64[1];
            HEAPU32[buf + 64 >> 2] = mtime % 1e3 * 1e3 * 1e3;
            tempI64 = [
              Math.floor(ctime / 1e3) >>> 0,
              (tempDouble = Math.floor(ctime / 1e3), +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil(
                (tempDouble - +(~~tempDouble >>> 0)) / 4294967296
              ) >>> 0 : 0)
            ], HEAP32[buf + 72 >> 2] = tempI64[0], HEAP32[buf + 76 >> 2] = tempI64[1];
            HEAPU32[buf + 80 >> 2] = ctime % 1e3 * 1e3 * 1e3;
            tempI64 = [
              stat.ino >>> 0,
              (tempDouble = stat.ino, +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil(
                (tempDouble - +(~~tempDouble >>> 0)) / 4294967296
              ) >>> 0 : 0)
            ], HEAP32[buf + 88 >> 2] = tempI64[0], HEAP32[buf + 92 >> 2] = tempI64[1];
            return 0;
          },
          doMsync(addr, stream, len, flags, offset) {
            if (!FS.isFile(stream.node.mode)) {
              throw new FS.ErrnoError(43);
            }
            if (flags & 2) {
              return 0;
            }
            var buffer = HEAPU8.slice(addr, addr + len);
            FS.msync(stream, buffer, offset, len, flags);
          },
          getStreamFromFD(fd) {
            var stream = FS.getStreamChecked(fd);
            return stream;
          },
          varargs: void 0,
          getStr(ptr) {
            var ret = UTF8ToString(ptr);
            return ret;
          }
        };
        function ___syscall_fcntl64(fd, cmd, varargs) {
          SYSCALLS.varargs = varargs;
          try {
            var stream = SYSCALLS.getStreamFromFD(fd);
            switch (cmd) {
              case 0: {
                var arg = syscallGetVarargI();
                if (arg < 0) {
                  return -28;
                }
                while (FS.streams[arg]) {
                  arg++;
                }
                var newStream;
                newStream = FS.dupStream(stream, arg);
                return newStream.fd;
              }
              case 1:
              case 2:
                return 0;
              // FD_CLOEXEC makes no sense for a single process.
              case 3:
                return stream.flags;
              case 4: {
                var arg = syscallGetVarargI();
                stream.flags |= arg;
                return 0;
              }
              case 12: {
                var arg = syscallGetVarargP();
                var offset = 0;
                HEAP16[arg + offset >> 1] = 2;
                return 0;
              }
              case 13:
              case 14:
                return 0;
            }
            return -28;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_fstat64(fd, buf) {
          try {
            var stream = SYSCALLS.getStreamFromFD(fd);
            return SYSCALLS.doStat(FS.stat, stream.path, buf);
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        var stringToUTF8 = (str, outPtr, maxBytesToWrite) => {
          assert(
            typeof maxBytesToWrite == "number",
            "stringToUTF8(str, outPtr, maxBytesToWrite) is missing the third parameter that specifies the length of the output buffer!"
          );
          return stringToUTF8Array(str, HEAPU8, outPtr, maxBytesToWrite);
        };
        function ___syscall_getdents64(fd, dirp, count) {
          try {
            var stream = SYSCALLS.getStreamFromFD(fd);
            stream.getdents ||= FS.readdir(stream.path);
            var struct_size = 280;
            var pos = 0;
            var off = FS.llseek(stream, 0, 1);
            var idx = Math.floor(off / struct_size);
            while (idx < stream.getdents.length && pos + struct_size <= count) {
              var id;
              var type;
              var name = stream.getdents[idx];
              if (name === ".") {
                id = stream.node.id;
                type = 4;
              } else if (name === "..") {
                var lookup = FS.lookupPath(stream.path, { parent: true });
                id = lookup.node.id;
                type = 4;
              } else {
                var child = FS.lookupNode(stream.node, name);
                id = child.id;
                type = FS.isChrdev(child.mode) ? 2 : (
                  // DT_CHR, character device.
                  FS.isDir(child.mode) ? 4 : (
                    // DT_DIR, directory.
                    FS.isLink(child.mode) ? 10 : (
                      // DT_LNK, symbolic link.
                      8
                    )
                  )
                );
              }
              assert(id);
              tempI64 = [
                id >>> 0,
                (tempDouble = id, +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil(
                  (tempDouble - +(~~tempDouble >>> 0)) / 4294967296
                ) >>> 0 : 0)
              ], HEAP32[dirp + pos >> 2] = tempI64[0], HEAP32[dirp + pos + 4 >> 2] = tempI64[1];
              tempI64 = [
                (idx + 1) * struct_size >>> 0,
                (tempDouble = (idx + 1) * struct_size, +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil(
                  (tempDouble - +(~~tempDouble >>> 0)) / 4294967296
                ) >>> 0 : 0)
              ], HEAP32[dirp + pos + 8 >> 2] = tempI64[0], HEAP32[dirp + pos + 12 >> 2] = tempI64[1];
              HEAP16[dirp + pos + 16 >> 1] = 280;
              HEAP8[dirp + pos + 18] = type;
              stringToUTF8(name, dirp + pos + 19, 256);
              pos += struct_size;
              idx += 1;
            }
            FS.llseek(stream, idx * struct_size, 0);
            return pos;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_ioctl(fd, op, varargs) {
          SYSCALLS.varargs = varargs;
          try {
            var stream = SYSCALLS.getStreamFromFD(fd);
            switch (op) {
              case 21509: {
                if (!stream.tty) return -59;
                return 0;
              }
              case 21505: {
                if (!stream.tty) return -59;
                if (stream.tty.ops.ioctl_tcgets) {
                  var termios = stream.tty.ops.ioctl_tcgets(stream);
                  var argp = syscallGetVarargP();
                  HEAP32[argp >> 2] = termios.c_iflag || 0;
                  HEAP32[argp + 4 >> 2] = termios.c_oflag || 0;
                  HEAP32[argp + 8 >> 2] = termios.c_cflag || 0;
                  HEAP32[argp + 12 >> 2] = termios.c_lflag || 0;
                  for (var i = 0; i < 32; i++) {
                    HEAP8[argp + i + 17] = termios.c_cc[i] || 0;
                  }
                  return 0;
                }
                return 0;
              }
              case 21510:
              case 21511:
              case 21512: {
                if (!stream.tty) return -59;
                return 0;
              }
              case 21506:
              case 21507:
              case 21508: {
                if (!stream.tty) return -59;
                if (stream.tty.ops.ioctl_tcsets) {
                  var argp = syscallGetVarargP();
                  var c_iflag = HEAP32[argp >> 2];
                  var c_oflag = HEAP32[argp + 4 >> 2];
                  var c_cflag = HEAP32[argp + 8 >> 2];
                  var c_lflag = HEAP32[argp + 12 >> 2];
                  var c_cc = [];
                  for (var i = 0; i < 32; i++) {
                    c_cc.push(HEAP8[argp + i + 17]);
                  }
                  return stream.tty.ops.ioctl_tcsets(stream.tty, op, {
                    c_iflag,
                    c_oflag,
                    c_cflag,
                    c_lflag,
                    c_cc
                  });
                }
                return 0;
              }
              case 21519: {
                if (!stream.tty) return -59;
                var argp = syscallGetVarargP();
                HEAP32[argp >> 2] = 0;
                return 0;
              }
              case 21520: {
                if (!stream.tty) return -59;
                return -28;
              }
              case 21531: {
                var argp = syscallGetVarargP();
                return FS.ioctl(stream, op, argp);
              }
              case 21523: {
                if (!stream.tty) return -59;
                if (stream.tty.ops.ioctl_tiocgwinsz) {
                  var winsize = stream.tty.ops.ioctl_tiocgwinsz(stream.tty);
                  var argp = syscallGetVarargP();
                  HEAP16[argp >> 1] = winsize[0];
                  HEAP16[argp + 2 >> 1] = winsize[1];
                }
                return 0;
              }
              case 21524: {
                if (!stream.tty) return -59;
                return 0;
              }
              case 21515: {
                if (!stream.tty) return -59;
                return 0;
              }
              default:
                return -28;
            }
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_lstat64(path, buf) {
          try {
            path = SYSCALLS.getStr(path);
            return SYSCALLS.doStat(FS.lstat, path, buf);
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_mkdirat(dirfd, path, mode) {
          try {
            path = SYSCALLS.getStr(path);
            path = SYSCALLS.calculateAt(dirfd, path);
            path = PATH.normalize(path);
            if (path[path.length - 1] === "/")
              path = path.substr(0, path.length - 1);
            FS.mkdir(path, mode, 0);
            return 0;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_newfstatat(dirfd, path, buf, flags) {
          try {
            path = SYSCALLS.getStr(path);
            var nofollow = flags & 256;
            var allowEmpty = flags & 4096;
            flags = flags & ~6400;
            assert(!flags, `unknown flags in __syscall_newfstatat: ${flags}`);
            path = SYSCALLS.calculateAt(dirfd, path, allowEmpty);
            return SYSCALLS.doStat(nofollow ? FS.lstat : FS.stat, path, buf);
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_openat(dirfd, path, flags, varargs) {
          SYSCALLS.varargs = varargs;
          try {
            path = SYSCALLS.getStr(path);
            path = SYSCALLS.calculateAt(dirfd, path);
            var mode = varargs ? syscallGetVarargI() : 0;
            return FS.open(path, flags, mode).fd;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_renameat(olddirfd, oldpath, newdirfd, newpath) {
          try {
            oldpath = SYSCALLS.getStr(oldpath);
            newpath = SYSCALLS.getStr(newpath);
            oldpath = SYSCALLS.calculateAt(olddirfd, oldpath);
            newpath = SYSCALLS.calculateAt(newdirfd, newpath);
            FS.rename(oldpath, newpath);
            return 0;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_rmdir(path) {
          try {
            path = SYSCALLS.getStr(path);
            FS.rmdir(path);
            return 0;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_stat64(path, buf) {
          try {
            path = SYSCALLS.getStr(path);
            return SYSCALLS.doStat(FS.stat, path, buf);
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        function ___syscall_unlinkat(dirfd, path, flags) {
          try {
            path = SYSCALLS.getStr(path);
            path = SYSCALLS.calculateAt(dirfd, path);
            if (flags === 0) {
              FS.unlink(path);
            } else if (flags === 512) {
              FS.rmdir(path);
            } else {
              abort("Invalid flags passed to unlinkat");
            }
            return 0;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return -e.errno;
          }
        }
        var __abort_js = () => {
          abort("native code called abort()");
        };
        var __emscripten_memcpy_js = (dest, src, num) => HEAPU8.copyWithin(dest, src, src + num);
        var __emscripten_runtime_keepalive_clear = () => {
          noExitRuntime = false;
          runtimeKeepaliveCounter = 0;
        };
        var timers = {};
        var handleException = (e) => {
          if (e instanceof ExitStatus || e == "unwind") {
            return EXITSTATUS;
          }
          checkStackCookie();
          if (e instanceof WebAssembly.RuntimeError) {
            if (_emscripten_stack_get_current() <= 0) {
              err(
                "Stack overflow detected.  You can try increasing -sSTACK_SIZE (currently set to 655360)"
              );
            }
          }
          quit_(1, e);
        };
        var runtimeKeepaliveCounter = 0;
        var keepRuntimeAlive = () => noExitRuntime || runtimeKeepaliveCounter > 0;
        var _proc_exit = (code) => {
          EXITSTATUS = code;
          if (!keepRuntimeAlive()) {
            Module["onExit"]?.(code);
            ABORT = true;
          }
          quit_(code, new ExitStatus(code));
        };
        var exitJS = (status, implicit) => {
          EXITSTATUS = status;
          checkUnflushedContent();
          if (keepRuntimeAlive() && !implicit) {
            var msg = `program exited (with status: ${status}), but keepRuntimeAlive() is set (counter=${runtimeKeepaliveCounter}) due to an async operation, so halting execution but not exiting the runtime or preventing further async execution (you can use emscripten_force_exit, if you want to force a true shutdown)`;
            readyPromiseReject(msg);
            err(msg);
          }
          _proc_exit(status);
        };
        var _exit = exitJS;
        var maybeExit = () => {
          if (!keepRuntimeAlive()) {
            try {
              _exit(EXITSTATUS);
            } catch (e) {
              handleException(e);
            }
          }
        };
        var callUserCallback = (func) => {
          if (ABORT) {
            err(
              "user callback triggered after runtime exited or application aborted.  Ignoring."
            );
            return;
          }
          try {
            func();
            maybeExit();
          } catch (e) {
            handleException(e);
          }
        };
        var _emscripten_get_now = () => performance.now();
        var __setitimer_js = (which, timeout_ms) => {
          if (timers[which]) {
            clearTimeout(timers[which].id);
            delete timers[which];
          }
          if (!timeout_ms) return 0;
          var id = setTimeout(() => {
            assert(which in timers);
            delete timers[which];
            callUserCallback(
              () => __emscripten_timeout(which, _emscripten_get_now())
            );
          }, timeout_ms);
          timers[which] = { id, timeout_ms };
          return 0;
        };
        var _emscripten_date_now = () => Date.now();
        var getHeapMax = () => (
          // Stay one Wasm page short of 4GB: while e.g. Chrome is able to allocate
          // full 4GB Wasm memories, the size will wrap back to 0 bytes in Wasm side
          // for any code that deals with heap sizes, which would require special
          // casing all heap size related code to treat 0 specially.
          2147483648
        );
        var growMemory = (size) => {
          var b = wasmMemory.buffer;
          var pages = (size - b.byteLength + 65535) / 65536 | 0;
          try {
            wasmMemory.grow(pages);
            updateMemoryViews();
            return 1;
          } catch (e) {
            err(
              `growMemory: Attempted to grow heap from ${b.byteLength} bytes to ${size} bytes, but got error: ${e}`
            );
          }
        };
        var _emscripten_resize_heap = (requestedSize) => {
          var oldSize = HEAPU8.length;
          requestedSize >>>= 0;
          assert(requestedSize > oldSize);
          var maxHeapSize = getHeapMax();
          if (requestedSize > maxHeapSize) {
            err(
              `Cannot enlarge memory, requested ${requestedSize} bytes, but the limit is ${maxHeapSize} bytes!`
            );
            return false;
          }
          for (var cutDown = 1; cutDown <= 4; cutDown *= 2) {
            var overGrownHeapSize = oldSize * (1 + 0.2 / cutDown);
            overGrownHeapSize = Math.min(
              overGrownHeapSize,
              requestedSize + 100663296
            );
            var newSize = Math.min(
              maxHeapSize,
              alignMemory(Math.max(requestedSize, overGrownHeapSize), 65536)
            );
            var replacement = growMemory(newSize);
            if (replacement) {
              return true;
            }
          }
          err(
            `Failed to grow the heap from ${oldSize} bytes to ${newSize} bytes, not enough memory!`
          );
          return false;
        };
        var ENV = {};
        var getExecutableName = () => {
          return thisProgram || "./this.program";
        };
        var getEnvStrings = () => {
          if (!getEnvStrings.strings) {
            var lang = (typeof navigator == "object" && navigator.languages && navigator.languages[0] || "C").replace("-", "_") + ".UTF-8";
            var env = {
              USER: "web_user",
              LOGNAME: "web_user",
              PATH: "/",
              PWD: "/",
              HOME: "/home/web_user",
              LANG: lang,
              _: getExecutableName()
            };
            for (var x in ENV) {
              if (ENV[x] === void 0) delete env[x];
              else env[x] = ENV[x];
            }
            var strings = [];
            for (var x in env) {
              strings.push(`${x}=${env[x]}`);
            }
            getEnvStrings.strings = strings;
          }
          return getEnvStrings.strings;
        };
        var stringToAscii = (str, buffer) => {
          for (var i = 0; i < str.length; ++i) {
            assert(str.charCodeAt(i) === (str.charCodeAt(i) & 255));
            HEAP8[buffer++] = str.charCodeAt(i);
          }
          HEAP8[buffer] = 0;
        };
        var _environ_get = (__environ, environ_buf) => {
          var bufSize = 0;
          getEnvStrings().forEach((string, i) => {
            var ptr = environ_buf + bufSize;
            HEAPU32[__environ + i * 4 >> 2] = ptr;
            stringToAscii(string, ptr);
            bufSize += string.length + 1;
          });
          return 0;
        };
        var _environ_sizes_get = (penviron_count, penviron_buf_size) => {
          var strings = getEnvStrings();
          HEAPU32[penviron_count >> 2] = strings.length;
          var bufSize = 0;
          strings.forEach((string) => bufSize += string.length + 1);
          HEAPU32[penviron_buf_size >> 2] = bufSize;
          return 0;
        };
        function _fd_close(fd) {
          try {
            var stream = SYSCALLS.getStreamFromFD(fd);
            FS.close(stream);
            return 0;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return e.errno;
          }
        }
        var doReadv = (stream, iov, iovcnt, offset) => {
          var ret = 0;
          for (var i = 0; i < iovcnt; i++) {
            var ptr = HEAPU32[iov >> 2];
            var len = HEAPU32[iov + 4 >> 2];
            iov += 8;
            var curr = FS.read(stream, HEAP8, ptr, len, offset);
            if (curr < 0) return -1;
            ret += curr;
            if (curr < len) break;
            if (typeof offset != "undefined") {
              offset += curr;
            }
          }
          return ret;
        };
        function _fd_read(fd, iov, iovcnt, pnum) {
          try {
            var stream = SYSCALLS.getStreamFromFD(fd);
            var num = doReadv(stream, iov, iovcnt);
            HEAPU32[pnum >> 2] = num;
            return 0;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return e.errno;
          }
        }
        var convertI32PairToI53Checked = (lo, hi) => {
          assert(lo == lo >>> 0 || lo == (lo | 0));
          assert(hi === (hi | 0));
          return hi + 2097152 >>> 0 < 4194305 - !!lo ? (lo >>> 0) + hi * 4294967296 : NaN;
        };
        function _fd_seek(fd, offset_low, offset_high, whence, newOffset) {
          var offset = convertI32PairToI53Checked(offset_low, offset_high);
          try {
            if (isNaN(offset)) return 61;
            var stream = SYSCALLS.getStreamFromFD(fd);
            FS.llseek(stream, offset, whence);
            tempI64 = [
              stream.position >>> 0,
              (tempDouble = stream.position, +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil(
                (tempDouble - +(~~tempDouble >>> 0)) / 4294967296
              ) >>> 0 : 0)
            ], HEAP32[newOffset >> 2] = tempI64[0], HEAP32[newOffset + 4 >> 2] = tempI64[1];
            if (stream.getdents && offset === 0 && whence === 0)
              stream.getdents = null;
            return 0;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return e.errno;
          }
        }
        var doWritev = (stream, iov, iovcnt, offset) => {
          var ret = 0;
          for (var i = 0; i < iovcnt; i++) {
            var ptr = HEAPU32[iov >> 2];
            var len = HEAPU32[iov + 4 >> 2];
            iov += 8;
            var curr = FS.write(stream, HEAP8, ptr, len, offset);
            if (curr < 0) return -1;
            ret += curr;
            if (curr < len) {
              break;
            }
            if (typeof offset != "undefined") {
              offset += curr;
            }
          }
          return ret;
        };
        function _fd_write(fd, iov, iovcnt, pnum) {
          try {
            var stream = SYSCALLS.getStreamFromFD(fd);
            var num = doWritev(stream, iov, iovcnt);
            HEAPU32[pnum >> 2] = num;
            return 0;
          } catch (e) {
            if (typeof FS == "undefined" || !(e.name === "ErrnoError")) throw e;
            return e.errno;
          }
        }
        var stackAlloc = (sz) => __emscripten_stack_alloc(sz);
        var stringToUTF8OnStack = (str) => {
          var size = lengthBytesUTF8(str) + 1;
          var ret = stackAlloc(size);
          stringToUTF8(str, ret, size);
          return ret;
        };
        FS.createPreloadedFile = FS_createPreloadedFile;
        FS.staticInit();
        function checkIncomingModuleAPI() {
          ignoredModuleProp("fetchSettings");
        }
        var wasmImports = {
          /** @export */
          __assert_fail: ___assert_fail,
          /** @export */
          __call_sighandler: ___call_sighandler,
          /** @export */
          __syscall_fcntl64: ___syscall_fcntl64,
          /** @export */
          __syscall_fstat64: ___syscall_fstat64,
          /** @export */
          __syscall_getdents64: ___syscall_getdents64,
          /** @export */
          __syscall_ioctl: ___syscall_ioctl,
          /** @export */
          __syscall_lstat64: ___syscall_lstat64,
          /** @export */
          __syscall_mkdirat: ___syscall_mkdirat,
          /** @export */
          __syscall_newfstatat: ___syscall_newfstatat,
          /** @export */
          __syscall_openat: ___syscall_openat,
          /** @export */
          __syscall_renameat: ___syscall_renameat,
          /** @export */
          __syscall_rmdir: ___syscall_rmdir,
          /** @export */
          __syscall_stat64: ___syscall_stat64,
          /** @export */
          __syscall_unlinkat: ___syscall_unlinkat,
          /** @export */
          _abort_js: __abort_js,
          /** @export */
          _emscripten_memcpy_js: __emscripten_memcpy_js,
          /** @export */
          _emscripten_runtime_keepalive_clear: __emscripten_runtime_keepalive_clear,
          /** @export */
          _setitimer_js: __setitimer_js,
          /** @export */
          emscripten_date_now: _emscripten_date_now,
          /** @export */
          emscripten_resize_heap: _emscripten_resize_heap,
          /** @export */
          environ_get: _environ_get,
          /** @export */
          environ_sizes_get: _environ_sizes_get,
          /** @export */
          exit: _exit,
          /** @export */
          fd_close: _fd_close,
          /** @export */
          fd_read: _fd_read,
          /** @export */
          fd_seek: _fd_seek,
          /** @export */
          fd_write: _fd_write,
          /** @export */
          proc_exit: _proc_exit
        };
        var wasmExports = createWasm();
        var ___wasm_call_ctors = createExportWrapper("__wasm_call_ctors", 0);
        var _main = Module["_main"] = createExportWrapper("__main_argc_argv", 2);
        var _fflush = createExportWrapper("fflush", 1);
        var __emscripten_timeout = createExportWrapper("_emscripten_timeout", 2);
        var _strerror = createExportWrapper("strerror", 1);
        var _emscripten_stack_init = () => (_emscripten_stack_init = wasmExports["emscripten_stack_init"])();
        var _emscripten_stack_get_free = () => (_emscripten_stack_get_free = wasmExports["emscripten_stack_get_free"])();
        var _emscripten_stack_get_base = () => (_emscripten_stack_get_base = wasmExports["emscripten_stack_get_base"])();
        var _emscripten_stack_get_end = () => (_emscripten_stack_get_end = wasmExports["emscripten_stack_get_end"])();
        var __emscripten_stack_restore = (a0) => (__emscripten_stack_restore = wasmExports["_emscripten_stack_restore"])(
          a0
        );
        var __emscripten_stack_alloc = (a0) => (__emscripten_stack_alloc = wasmExports["_emscripten_stack_alloc"])(a0);
        var _emscripten_stack_get_current = () => (_emscripten_stack_get_current = wasmExports["emscripten_stack_get_current"])();
        var dynCall_jiji = Module["dynCall_jiji"] = createExportWrapper(
          "dynCall_jiji",
          5
        );
        Module["run"] = run;
        Module["callMain"] = callMain;
        Module["FS"] = FS;
        var missingLibrarySymbols = [
          "writeI53ToI64",
          "writeI53ToI64Clamped",
          "writeI53ToI64Signaling",
          "writeI53ToU64Clamped",
          "writeI53ToU64Signaling",
          "readI53FromI64",
          "readI53FromU64",
          "convertI32PairToI53",
          "convertU32PairToI53",
          "getTempRet0",
          "setTempRet0",
          "inetPton4",
          "inetNtop4",
          "inetPton6",
          "inetNtop6",
          "readSockaddr",
          "writeSockaddr",
          "emscriptenLog",
          "readEmAsmArgs",
          "jstoi_q",
          "listenOnce",
          "autoResumeAudioContext",
          "dynCallLegacy",
          "getDynCaller",
          "dynCall",
          "runtimeKeepalivePush",
          "runtimeKeepalivePop",
          "asmjsMangle",
          "HandleAllocator",
          "getNativeTypeSize",
          "STACK_SIZE",
          "STACK_ALIGN",
          "POINTER_SIZE",
          "ASSERTIONS",
          "getCFunc",
          "ccall",
          "cwrap",
          "uleb128Encode",
          "sigToWasmTypes",
          "generateFuncType",
          "convertJsFunctionToWasm",
          "getEmptyTableSlot",
          "updateTableMap",
          "getFunctionAddress",
          "addFunction",
          "removeFunction",
          "reallyNegative",
          "unSign",
          "strLen",
          "reSign",
          "formatString",
          "intArrayToString",
          "AsciiToString",
          "UTF16ToString",
          "stringToUTF16",
          "lengthBytesUTF16",
          "UTF32ToString",
          "stringToUTF32",
          "lengthBytesUTF32",
          "stringToNewUTF8",
          "writeArrayToMemory",
          "registerKeyEventCallback",
          "maybeCStringToJsString",
          "findEventTarget",
          "getBoundingClientRect",
          "fillMouseEventData",
          "registerMouseEventCallback",
          "registerWheelEventCallback",
          "registerUiEventCallback",
          "registerFocusEventCallback",
          "fillDeviceOrientationEventData",
          "registerDeviceOrientationEventCallback",
          "fillDeviceMotionEventData",
          "registerDeviceMotionEventCallback",
          "screenOrientation",
          "fillOrientationChangeEventData",
          "registerOrientationChangeEventCallback",
          "fillFullscreenChangeEventData",
          "registerFullscreenChangeEventCallback",
          "JSEvents_requestFullscreen",
          "JSEvents_resizeCanvasForFullscreen",
          "registerRestoreOldStyle",
          "hideEverythingExceptGivenElement",
          "restoreHiddenElements",
          "setLetterbox",
          "softFullscreenResizeWebGLRenderTarget",
          "doRequestFullscreen",
          "fillPointerlockChangeEventData",
          "registerPointerlockChangeEventCallback",
          "registerPointerlockErrorEventCallback",
          "requestPointerLock",
          "fillVisibilityChangeEventData",
          "registerVisibilityChangeEventCallback",
          "registerTouchEventCallback",
          "fillGamepadEventData",
          "registerGamepadEventCallback",
          "registerBeforeUnloadEventCallback",
          "fillBatteryEventData",
          "battery",
          "registerBatteryEventCallback",
          "setCanvasElementSize",
          "getCanvasElementSize",
          "jsStackTrace",
          "getCallstack",
          "convertPCtoSourceLocation",
          "checkWasiClock",
          "wasiRightsToMuslOFlags",
          "wasiOFlagsToMuslOFlags",
          "createDyncallWrapper",
          "safeSetTimeout",
          "setImmediateWrapped",
          "clearImmediateWrapped",
          "polyfillSetImmediate",
          "registerPostMainLoop",
          "registerPreMainLoop",
          "getPromise",
          "makePromise",
          "idsToPromises",
          "makePromiseCallback",
          "ExceptionInfo",
          "findMatchingCatch",
          "Browser_asyncPrepareDataCounter",
          "safeRequestAnimationFrame",
          "isLeapYear",
          "ydayFromDate",
          "arraySum",
          "addDays",
          "getSocketFromFD",
          "getSocketAddress",
          "FS_unlink",
          "FS_mkdirTree",
          "_setNetworkCallback",
          "heapObjectForWebGLType",
          "toTypedArrayIndex",
          "webgl_enable_ANGLE_instanced_arrays",
          "webgl_enable_OES_vertex_array_object",
          "webgl_enable_WEBGL_draw_buffers",
          "webgl_enable_WEBGL_multi_draw",
          "webgl_enable_EXT_polygon_offset_clamp",
          "webgl_enable_EXT_clip_control",
          "webgl_enable_WEBGL_polygon_mode",
          "emscriptenWebGLGet",
          "computeUnpackAlignedImageSize",
          "colorChannelsInGlTextureFormat",
          "emscriptenWebGLGetTexPixelData",
          "emscriptenWebGLGetUniform",
          "webglGetUniformLocation",
          "webglPrepareUniformLocationsBeforeFirstUse",
          "webglGetLeftBracePos",
          "emscriptenWebGLGetVertexAttrib",
          "__glGetActiveAttribOrUniform",
          "writeGLArray",
          "registerWebGlEventCallback",
          "runAndAbortIfError",
          "ALLOC_NORMAL",
          "ALLOC_STACK",
          "allocate",
          "writeStringToMemory",
          "writeAsciiToMemory",
          "setErrNo",
          "demangle",
          "stackTrace"
        ];
        missingLibrarySymbols.forEach(missingLibrarySymbol);
        var unexportedSymbols = [
          "addOnPreRun",
          "addOnInit",
          "addOnPreMain",
          "addOnExit",
          "addOnPostRun",
          "addRunDependency",
          "removeRunDependency",
          "out",
          "err",
          "abort",
          "wasmMemory",
          "wasmExports",
          "writeStackCookie",
          "checkStackCookie",
          "convertI32PairToI53Checked",
          "stackSave",
          "stackRestore",
          "stackAlloc",
          "ptrToString",
          "zeroMemory",
          "exitJS",
          "getHeapMax",
          "growMemory",
          "ENV",
          "ERRNO_CODES",
          "strError",
          "DNS",
          "Protocols",
          "Sockets",
          "initRandomFill",
          "randomFill",
          "timers",
          "warnOnce",
          "readEmAsmArgsArray",
          "jstoi_s",
          "getExecutableName",
          "handleException",
          "keepRuntimeAlive",
          "callUserCallback",
          "maybeExit",
          "asyncLoad",
          "alignMemory",
          "mmapAlloc",
          "wasmTable",
          "noExitRuntime",
          "freeTableIndexes",
          "functionsInTableMap",
          "setValue",
          "getValue",
          "PATH",
          "PATH_FS",
          "UTF8Decoder",
          "UTF8ArrayToString",
          "UTF8ToString",
          "stringToUTF8Array",
          "stringToUTF8",
          "lengthBytesUTF8",
          "intArrayFromString",
          "stringToAscii",
          "UTF16Decoder",
          "stringToUTF8OnStack",
          "JSEvents",
          "specialHTMLTargets",
          "findCanvasEventTarget",
          "currentFullscreenStrategy",
          "restoreOldWindowedStyle",
          "UNWIND_CACHE",
          "ExitStatus",
          "getEnvStrings",
          "doReadv",
          "doWritev",
          "promiseMap",
          "uncaughtExceptionCount",
          "exceptionLast",
          "exceptionCaught",
          "Browser",
          "getPreloadedImageData__data",
          "wget",
          "MONTH_DAYS_REGULAR",
          "MONTH_DAYS_LEAP",
          "MONTH_DAYS_REGULAR_CUMULATIVE",
          "MONTH_DAYS_LEAP_CUMULATIVE",
          "SYSCALLS",
          "preloadPlugins",
          "FS_createPreloadedFile",
          "FS_modeStringToFlags",
          "FS_getMode",
          "FS_stdin_getChar_buffer",
          "FS_stdin_getChar",
          "FS_createPath",
          "FS_createDevice",
          "FS_readFile",
          "FS_createDataFile",
          "FS_createLazyFile",
          "MEMFS",
          "TTY",
          "PIPEFS",
          "SOCKFS",
          "tempFixedLengthArray",
          "miniTempWebGLFloatBuffers",
          "miniTempWebGLIntBuffers",
          "GL",
          "AL",
          "GLUT",
          "EGL",
          "GLEW",
          "IDBStore",
          "SDL",
          "SDL_gfx",
          "allocateUTF8",
          "allocateUTF8OnStack",
          "print",
          "printErr"
        ];
        unexportedSymbols.forEach(unexportedRuntimeSymbol);
        var calledRun;
        var calledPrerun;
        dependenciesFulfilled = function runCaller() {
          if (!calledRun) run();
          if (!calledRun) dependenciesFulfilled = runCaller;
        };
        function callMain(args = []) {
          assert(
            runDependencies == 0,
            'cannot call main when async dependencies remain! (listen on Module["onRuntimeInitialized"])'
          );
          assert(calledPrerun, "cannot call main without calling preRun first");
          var entryFunction = _main;
          args.unshift(thisProgram);
          var argc = args.length;
          var argv = stackAlloc((argc + 1) * 4);
          var argv_ptr = argv;
          args.forEach((arg) => {
            HEAPU32[argv_ptr >> 2] = stringToUTF8OnStack(arg);
            argv_ptr += 4;
          });
          HEAPU32[argv_ptr >> 2] = 0;
          try {
            var ret = entryFunction(argc, argv);
            exitJS(
              ret,
              /* implicit = */
              true
            );
            return ret;
          } catch (e) {
            return handleException(e);
          }
        }
        function stackCheckInit() {
          _emscripten_stack_init();
          writeStackCookie();
        }
        function run(args = arguments_) {
          if (runDependencies > 0) {
            return;
          }
          stackCheckInit();
          if (!calledPrerun) {
            calledPrerun = 1;
            preRun();
            if (runDependencies > 0) {
              return;
            }
          }
          function doRun() {
            if (calledRun) return;
            calledRun = 1;
            Module["calledRun"] = 1;
            if (ABORT) return;
            initRuntime();
            preMain();
            readyPromiseResolve(Module);
            Module["onRuntimeInitialized"]?.();
            if (shouldRunNow) callMain(args);
            postRun();
          }
          if (Module["setStatus"]) {
            Module["setStatus"]("Running...");
            setTimeout(() => {
              setTimeout(() => Module["setStatus"](""), 1);
              doRun();
            }, 1);
          } else {
            doRun();
          }
          checkStackCookie();
        }
        function checkUnflushedContent() {
          var oldOut = out;
          var oldErr = err;
          var has = false;
          out = err = (x) => {
            has = true;
          };
          try {
            _fflush(0);
            ["stdout", "stderr"].forEach((name) => {
              var info = FS.analyzePath("/dev/" + name);
              if (!info) return;
              var stream = info.object;
              var rdev = stream.rdev;
              var tty = TTY.ttys[rdev];
              if (tty?.output?.length) {
                has = true;
              }
            });
          } catch (e) {
          }
          out = oldOut;
          err = oldErr;
          if (has) {
            warnOnce(
              "stdio streams had content in them that was not flushed. you should set EXIT_RUNTIME to 1 (see the Emscripten FAQ), or make sure to emit a newline when you printf etc."
            );
          }
        }
        if (Module["preInit"]) {
          if (typeof Module["preInit"] == "function")
            Module["preInit"] = [Module["preInit"]];
          while (Module["preInit"].length > 0) {
            Module["preInit"].pop()();
          }
        }
        var shouldRunNow = true;
        if (Module["noInitialRun"]) shouldRunNow = false;
        run();
        moduleRtn = readyPromise;
        for (const prop of Object.keys(Module)) {
          if (!(prop in moduleArg)) {
            Object.defineProperty(moduleArg, prop, {
              configurable: true,
              get() {
                abort(
                  `Access to module property ('${prop}') is no longer possible via the module constructor argument; Instead, use the result of the module constructor.`
                );
              }
            });
          }
        }
        return moduleRtn;
      };
    })();
    if (typeof exports === "object" && typeof module === "object")
      module.exports = hacbrewpack;
    else if (typeof define === "function" && define["amd"])
      define([], () => hacbrewpack);
  }
});

// node_modules/.pnpm/@nx.js+constants@0.4.0/node_modules/@nx.js/constants/dist/applet.js
var AppletType;
(function(AppletType2) {
  AppletType2[AppletType2["None"] = -2] = "None";
  AppletType2[AppletType2["Default"] = -1] = "Default";
  AppletType2[AppletType2["Application"] = 0] = "Application";
  AppletType2[AppletType2["SystemApplet"] = 1] = "SystemApplet";
  AppletType2[AppletType2["LibraryApplet"] = 2] = "LibraryApplet";
  AppletType2[AppletType2["OverlayApplet"] = 3] = "OverlayApplet";
  AppletType2[AppletType2["SystemApplication"] = 4] = "SystemApplication";
})(AppletType || (AppletType = {}));
var OperationMode;
(function(OperationMode2) {
  OperationMode2[OperationMode2["Handheld"] = 0] = "Handheld";
  OperationMode2[OperationMode2["Console"] = 1] = "Console";
})(OperationMode || (OperationMode = {}));

// node_modules/.pnpm/@nx.js+constants@0.4.0/node_modules/@nx.js/constants/dist/fs.js
var FsSaveDataSpaceId;
(function(FsSaveDataSpaceId2) {
  FsSaveDataSpaceId2[FsSaveDataSpaceId2["System"] = 0] = "System";
  FsSaveDataSpaceId2[FsSaveDataSpaceId2["User"] = 1] = "User";
  FsSaveDataSpaceId2[FsSaveDataSpaceId2["SdSystem"] = 2] = "SdSystem";
  FsSaveDataSpaceId2[FsSaveDataSpaceId2["Temporary"] = 3] = "Temporary";
  FsSaveDataSpaceId2[FsSaveDataSpaceId2["SdUser"] = 4] = "SdUser";
  FsSaveDataSpaceId2[FsSaveDataSpaceId2["ProperSystem"] = 100] = "ProperSystem";
  FsSaveDataSpaceId2[FsSaveDataSpaceId2["SafeMode"] = 101] = "SafeMode";
})(FsSaveDataSpaceId || (FsSaveDataSpaceId = {}));
var FsSaveDataType;
(function(FsSaveDataType2) {
  FsSaveDataType2[FsSaveDataType2["System"] = 0] = "System";
  FsSaveDataType2[FsSaveDataType2["Account"] = 1] = "Account";
  FsSaveDataType2[FsSaveDataType2["Bcat"] = 2] = "Bcat";
  FsSaveDataType2[FsSaveDataType2["Device"] = 3] = "Device";
  FsSaveDataType2[FsSaveDataType2["Temporary"] = 4] = "Temporary";
  FsSaveDataType2[FsSaveDataType2["Cache"] = 5] = "Cache";
  FsSaveDataType2[FsSaveDataType2["SystemBcat"] = 6] = "SystemBcat";
})(FsSaveDataType || (FsSaveDataType = {}));
var BisPartitionId;
(function(BisPartitionId2) {
  BisPartitionId2[BisPartitionId2["BootPartition1Root"] = 0] = "BootPartition1Root";
  BisPartitionId2[BisPartitionId2["BootPartition2Root"] = 10] = "BootPartition2Root";
  BisPartitionId2[BisPartitionId2["UserDataRoot"] = 20] = "UserDataRoot";
  BisPartitionId2[BisPartitionId2["BootConfigAndPackage2Part1"] = 21] = "BootConfigAndPackage2Part1";
  BisPartitionId2[BisPartitionId2["BootConfigAndPackage2Part2"] = 22] = "BootConfigAndPackage2Part2";
  BisPartitionId2[BisPartitionId2["BootConfigAndPackage2Part3"] = 23] = "BootConfigAndPackage2Part3";
  BisPartitionId2[BisPartitionId2["BootConfigAndPackage2Part4"] = 24] = "BootConfigAndPackage2Part4";
  BisPartitionId2[BisPartitionId2["BootConfigAndPackage2Part5"] = 25] = "BootConfigAndPackage2Part5";
  BisPartitionId2[BisPartitionId2["BootConfigAndPackage2Part6"] = 26] = "BootConfigAndPackage2Part6";
  BisPartitionId2[BisPartitionId2["CalibrationBinary"] = 27] = "CalibrationBinary";
  BisPartitionId2[BisPartitionId2["CalibrationFile"] = 28] = "CalibrationFile";
  BisPartitionId2[BisPartitionId2["SafeMode"] = 29] = "SafeMode";
  BisPartitionId2[BisPartitionId2["User"] = 30] = "User";
  BisPartitionId2[BisPartitionId2["System"] = 31] = "System";
  BisPartitionId2[BisPartitionId2["SystemProperEncryption"] = 32] = "SystemProperEncryption";
  BisPartitionId2[BisPartitionId2["SystemProperPartition"] = 33] = "SystemProperPartition";
  BisPartitionId2[BisPartitionId2["SignedSystemPartitionOnSafeMode"] = 34] = "SignedSystemPartitionOnSafeMode";
  BisPartitionId2[BisPartitionId2["DeviceTreeBlob"] = 35] = "DeviceTreeBlob";
  BisPartitionId2[BisPartitionId2["System0"] = 36] = "System0";
})(BisPartitionId || (BisPartitionId = {}));
var FsFileSystemType;
(function(FsFileSystemType2) {
  FsFileSystemType2[FsFileSystemType2["Logo"] = 2] = "Logo";
  FsFileSystemType2[FsFileSystemType2["ContentControl"] = 3] = "ContentControl";
  FsFileSystemType2[FsFileSystemType2["ContentManual"] = 4] = "ContentManual";
  FsFileSystemType2[FsFileSystemType2["ContentMeta"] = 5] = "ContentMeta";
  FsFileSystemType2[FsFileSystemType2["ContentData"] = 6] = "ContentData";
  FsFileSystemType2[FsFileSystemType2["ApplicationPackage"] = 7] = "ApplicationPackage";
  FsFileSystemType2[FsFileSystemType2["RegisteredUpdate"] = 8] = "RegisteredUpdate";
})(FsFileSystemType || (FsFileSystemType = {}));
var FsContentAttributes;
(function(FsContentAttributes2) {
  FsContentAttributes2[FsContentAttributes2["None"] = 0] = "None";
  FsContentAttributes2[FsContentAttributes2["All"] = 15] = "All";
})(FsContentAttributes || (FsContentAttributes = {}));

// node_modules/.pnpm/@nx.js+constants@0.4.0/node_modules/@nx.js/constants/dist/hid.js
var HidNpadButton;
(function(HidNpadButton2) {
  HidNpadButton2[HidNpadButton2["A"] = 1] = "A";
  HidNpadButton2[HidNpadButton2["B"] = 2] = "B";
  HidNpadButton2[HidNpadButton2["X"] = 4] = "X";
  HidNpadButton2[HidNpadButton2["Y"] = 8] = "Y";
  HidNpadButton2[HidNpadButton2["StickL"] = 16] = "StickL";
  HidNpadButton2[HidNpadButton2["StickR"] = 32] = "StickR";
  HidNpadButton2[HidNpadButton2["L"] = 64] = "L";
  HidNpadButton2[HidNpadButton2["R"] = 128] = "R";
  HidNpadButton2[HidNpadButton2["ZL"] = 256] = "ZL";
  HidNpadButton2[HidNpadButton2["ZR"] = 512] = "ZR";
  HidNpadButton2[HidNpadButton2["Plus"] = 1024] = "Plus";
  HidNpadButton2[HidNpadButton2["Minus"] = 2048] = "Minus";
  HidNpadButton2[HidNpadButton2["Left"] = 4096] = "Left";
  HidNpadButton2[HidNpadButton2["Up"] = 8192] = "Up";
  HidNpadButton2[HidNpadButton2["Right"] = 16384] = "Right";
  HidNpadButton2[HidNpadButton2["Down"] = 32768] = "Down";
  HidNpadButton2[HidNpadButton2["StickLLeft"] = 65536] = "StickLLeft";
  HidNpadButton2[HidNpadButton2["StickLUp"] = 131072] = "StickLUp";
  HidNpadButton2[HidNpadButton2["StickLRight"] = 262144] = "StickLRight";
  HidNpadButton2[HidNpadButton2["StickLDown"] = 524288] = "StickLDown";
  HidNpadButton2[HidNpadButton2["StickRLeft"] = 1048576] = "StickRLeft";
  HidNpadButton2[HidNpadButton2["StickRUp"] = 2097152] = "StickRUp";
  HidNpadButton2[HidNpadButton2["StickRRight"] = 4194304] = "StickRRight";
  HidNpadButton2[HidNpadButton2["StickRDown"] = 8388608] = "StickRDown";
  HidNpadButton2[HidNpadButton2["LeftSL"] = 16777216] = "LeftSL";
  HidNpadButton2[HidNpadButton2["LeftSR"] = 33554432] = "LeftSR";
  HidNpadButton2[HidNpadButton2["RightSL"] = 67108864] = "RightSL";
  HidNpadButton2[HidNpadButton2["RightSR"] = 134217728] = "RightSR";
  HidNpadButton2[HidNpadButton2["Palma"] = 268435456] = "Palma";
  HidNpadButton2[HidNpadButton2["Verification"] = 536870912] = "Verification";
  HidNpadButton2[HidNpadButton2["HandheldLeftB"] = 1073741824] = "HandheldLeftB";
  HidNpadButton2[HidNpadButton2["LagonCLeft"] = -2147483648] = "LagonCLeft";
  HidNpadButton2[HidNpadButton2["LagonCUp"] = 1] = "LagonCUp";
  HidNpadButton2[HidNpadButton2["LagonCRight"] = 2] = "LagonCRight";
  HidNpadButton2[HidNpadButton2["LagonCDown"] = 4] = "LagonCDown";
  HidNpadButton2[HidNpadButton2["AnyLeft"] = 1118208] = "AnyLeft";
  HidNpadButton2[HidNpadButton2["AnyUp"] = 2236416] = "AnyUp";
  HidNpadButton2[HidNpadButton2["AnyRight"] = 4472832] = "AnyRight";
  HidNpadButton2[HidNpadButton2["AnyDown"] = 8945664] = "AnyDown";
  HidNpadButton2[HidNpadButton2["AnySL"] = 83886080] = "AnySL";
  HidNpadButton2[HidNpadButton2["AnySR"] = 167772160] = "AnySR";
})(HidNpadButton || (HidNpadButton = {}));
var HidDeviceTypeBits;
(function(HidDeviceTypeBits2) {
  HidDeviceTypeBits2[HidDeviceTypeBits2["FullKey"] = 1] = "FullKey";
  HidDeviceTypeBits2[HidDeviceTypeBits2["DebugPad"] = 2] = "DebugPad";
  HidDeviceTypeBits2[HidDeviceTypeBits2["HandheldLeft"] = 4] = "HandheldLeft";
  HidDeviceTypeBits2[HidDeviceTypeBits2["HandheldRight"] = 8] = "HandheldRight";
  HidDeviceTypeBits2[HidDeviceTypeBits2["JoyLeft"] = 16] = "JoyLeft";
  HidDeviceTypeBits2[HidDeviceTypeBits2["JoyRight"] = 32] = "JoyRight";
  HidDeviceTypeBits2[HidDeviceTypeBits2["Palma"] = 64] = "Palma";
  HidDeviceTypeBits2[HidDeviceTypeBits2["LarkHvcLeft"] = 128] = "LarkHvcLeft";
  HidDeviceTypeBits2[HidDeviceTypeBits2["LarkHvcRight"] = 256] = "LarkHvcRight";
  HidDeviceTypeBits2[HidDeviceTypeBits2["LarkNesLeft"] = 512] = "LarkNesLeft";
  HidDeviceTypeBits2[HidDeviceTypeBits2["LarkNesRight"] = 1024] = "LarkNesRight";
  HidDeviceTypeBits2[HidDeviceTypeBits2["HandheldLarkHvcLeft"] = 2048] = "HandheldLarkHvcLeft";
  HidDeviceTypeBits2[HidDeviceTypeBits2["HandheldLarkHvcRight"] = 4096] = "HandheldLarkHvcRight";
  HidDeviceTypeBits2[HidDeviceTypeBits2["HandheldLarkNesLeft"] = 8192] = "HandheldLarkNesLeft";
  HidDeviceTypeBits2[HidDeviceTypeBits2["HandheldLarkNesRight"] = 16384] = "HandheldLarkNesRight";
  HidDeviceTypeBits2[HidDeviceTypeBits2["Lucia"] = 32768] = "Lucia";
  HidDeviceTypeBits2[HidDeviceTypeBits2["Lagon"] = 65536] = "Lagon";
  HidDeviceTypeBits2[HidDeviceTypeBits2["Lager"] = 131072] = "Lager";
  HidDeviceTypeBits2[HidDeviceTypeBits2["System"] = -2147483648] = "System";
})(HidDeviceTypeBits || (HidDeviceTypeBits = {}));
var HidNpadStyleTag;
(function(HidNpadStyleTag2) {
  HidNpadStyleTag2[HidNpadStyleTag2["FullKey"] = 1] = "FullKey";
  HidNpadStyleTag2[HidNpadStyleTag2["Handheld"] = 2] = "Handheld";
  HidNpadStyleTag2[HidNpadStyleTag2["JoyDual"] = 4] = "JoyDual";
  HidNpadStyleTag2[HidNpadStyleTag2["JoyLeft"] = 8] = "JoyLeft";
  HidNpadStyleTag2[HidNpadStyleTag2["JoyRight"] = 16] = "JoyRight";
  HidNpadStyleTag2[HidNpadStyleTag2["Gc"] = 32] = "Gc";
  HidNpadStyleTag2[HidNpadStyleTag2["Palma"] = 64] = "Palma";
  HidNpadStyleTag2[HidNpadStyleTag2["Lark"] = 128] = "Lark";
  HidNpadStyleTag2[HidNpadStyleTag2["HandheldLark"] = 256] = "HandheldLark";
  HidNpadStyleTag2[HidNpadStyleTag2["Lucia"] = 512] = "Lucia";
  HidNpadStyleTag2[HidNpadStyleTag2["Lagon"] = 1024] = "Lagon";
  HidNpadStyleTag2[HidNpadStyleTag2["Lager"] = 2048] = "Lager";
  HidNpadStyleTag2[HidNpadStyleTag2["SystemExt"] = 536870912] = "SystemExt";
  HidNpadStyleTag2[HidNpadStyleTag2["System"] = 1073741824] = "System";
})(HidNpadStyleTag || (HidNpadStyleTag = {}));

// node_modules/.pnpm/@nx.js+constants@0.4.0/node_modules/@nx.js/constants/dist/service.js
var SfBufferAttr;
(function(SfBufferAttr2) {
  SfBufferAttr2[SfBufferAttr2["In"] = 1] = "In";
  SfBufferAttr2[SfBufferAttr2["Out"] = 2] = "Out";
  SfBufferAttr2[SfBufferAttr2["HipcMapAlias"] = 4] = "HipcMapAlias";
  SfBufferAttr2[SfBufferAttr2["HipcPointer"] = 8] = "HipcPointer";
  SfBufferAttr2[SfBufferAttr2["FixedSize"] = 16] = "FixedSize";
  SfBufferAttr2[SfBufferAttr2["HipcAutoSelect"] = 32] = "HipcAutoSelect";
  SfBufferAttr2[SfBufferAttr2["HipcMapTransferAllowsNonSecure"] = 64] = "HipcMapTransferAllowsNonSecure";
  SfBufferAttr2[SfBufferAttr2["HipcMapTransferAllowsNonDevice"] = 128] = "HipcMapTransferAllowsNonDevice";
})(SfBufferAttr || (SfBufferAttr = {}));
var SfOutHandleAttr;
(function(SfOutHandleAttr2) {
  SfOutHandleAttr2[SfOutHandleAttr2["None"] = 0] = "None";
  SfOutHandleAttr2[SfOutHandleAttr2["HipcCopy"] = 1] = "HipcCopy";
  SfOutHandleAttr2[SfOutHandleAttr2["HipcMove"] = 2] = "HipcMove";
})(SfOutHandleAttr || (SfOutHandleAttr = {}));

// node_modules/.pnpm/@nx.js+util@1.1.0/node_modules/@nx.js/util/dist/index.js
function createSingleton(init) {
  const map = /* @__PURE__ */ new WeakMap();
  return (k) => {
    if (!map.has(k)) {
      const v = init(k);
      map.set(k, v);
      return v;
    }
    return map.get(k);
  };
}
var view = createSingleton((view2) => new DataView(view2.buffer, view2.byteOffset, view2.byteLength));
var u8 = createSingleton((view2) => new Uint8Array(view2.buffer, view2.byteOffset, view2.byteLength));
function decodeCString(view2) {
  const arr = u8(view2);
  const nul = arr.indexOf(0);
  return new TextDecoder().decode(nul !== -1 ? arr.subarray(0, nul) : arr);
}
var ArrayBufferStruct = class {
  buffer;
  byteLength;
  byteOffset;
  static sizeof = 0;
  constructor(buffer, byteOffset, byteLength) {
    let buf;
    let bo = 0;
    if (buffer) {
      if ("buffer" in buffer) {
        buf = buffer.buffer;
        bo = buffer.byteOffset;
      } else {
        buf = buffer;
      }
    } else {
      buf = new ArrayBuffer(new.target.sizeof);
    }
    if (typeof byteOffset === "number") {
      bo += byteOffset;
    }
    Object.defineProperties(this, {
      buffer: { value: buf, enumerable: false },
      byteLength: { value: byteLength ?? new.target.sizeof, enumerable: false },
      byteOffset: { value: bo, enumerable: false }
    });
  }
  /**
   * Creates a copy of the struct buffer view as a new `ArrayBuffer` instance.
   */
  toArrayBuffer() {
    return this.buffer.slice(this.byteOffset, this.byteOffset + this.byteLength);
  }
};

// node_modules/.pnpm/@nx.js+ncm@1.2.0/node_modules/@nx.js/ncm/dist/service.js
var ncm = new Switch.Service("ncm");

// node_modules/.pnpm/@nx.js+ncm@1.2.0/node_modules/@nx.js/ncm/dist/types.js
var contentIdCache = createSingleton((self2) => new NcmContentId(self2, 0));
var contentMetaKeyCache = createSingleton((self2) => new NcmContentMetaKey(self2, 0));
var NcmStorageId;
(function(NcmStorageId2) {
  NcmStorageId2[NcmStorageId2["None"] = 0] = "None";
  NcmStorageId2[NcmStorageId2["Host"] = 1] = "Host";
  NcmStorageId2[NcmStorageId2["GameCard"] = 2] = "GameCard";
  NcmStorageId2[NcmStorageId2["BuiltInSystem"] = 3] = "BuiltInSystem";
  NcmStorageId2[NcmStorageId2["BuiltInUser"] = 4] = "BuiltInUser";
  NcmStorageId2[NcmStorageId2["SdCard"] = 5] = "SdCard";
  NcmStorageId2[NcmStorageId2["Any"] = 6] = "Any";
})(NcmStorageId || (NcmStorageId = {}));
var NcmContentType;
(function(NcmContentType2) {
  NcmContentType2[NcmContentType2["Meta"] = 0] = "Meta";
  NcmContentType2[NcmContentType2["Program"] = 1] = "Program";
  NcmContentType2[NcmContentType2["Data"] = 2] = "Data";
  NcmContentType2[NcmContentType2["Control"] = 3] = "Control";
  NcmContentType2[NcmContentType2["HtmlDocument"] = 4] = "HtmlDocument";
  NcmContentType2[NcmContentType2["LegalInformation"] = 5] = "LegalInformation";
  NcmContentType2[NcmContentType2["DeltaFragment"] = 6] = "DeltaFragment";
})(NcmContentType || (NcmContentType = {}));
var NcmContentMetaType;
(function(NcmContentMetaType2) {
  NcmContentMetaType2[NcmContentMetaType2["Unknown"] = 0] = "Unknown";
  NcmContentMetaType2[NcmContentMetaType2["SystemProgram"] = 1] = "SystemProgram";
  NcmContentMetaType2[NcmContentMetaType2["SystemData"] = 2] = "SystemData";
  NcmContentMetaType2[NcmContentMetaType2["SystemUpdate"] = 3] = "SystemUpdate";
  NcmContentMetaType2[NcmContentMetaType2["BootImagePackage"] = 4] = "BootImagePackage";
  NcmContentMetaType2[NcmContentMetaType2["BootImagePackageSafe"] = 5] = "BootImagePackageSafe";
  NcmContentMetaType2[NcmContentMetaType2["Application"] = 128] = "Application";
  NcmContentMetaType2[NcmContentMetaType2["Patch"] = 129] = "Patch";
  NcmContentMetaType2[NcmContentMetaType2["AddOnContent"] = 130] = "AddOnContent";
  NcmContentMetaType2[NcmContentMetaType2["Delta"] = 131] = "Delta";
  NcmContentMetaType2[NcmContentMetaType2["DataPatch"] = 132] = "DataPatch";
})(NcmContentMetaType || (NcmContentMetaType = {}));
var NcmContentMetaAttribute;
(function(NcmContentMetaAttribute2) {
  NcmContentMetaAttribute2[NcmContentMetaAttribute2["None"] = 0] = "None";
  NcmContentMetaAttribute2[NcmContentMetaAttribute2["IncludesExFatDriver"] = 1] = "IncludesExFatDriver";
  NcmContentMetaAttribute2[NcmContentMetaAttribute2["Rebootless"] = 2] = "Rebootless";
  NcmContentMetaAttribute2[NcmContentMetaAttribute2["Compacted"] = 4] = "Compacted";
})(NcmContentMetaAttribute || (NcmContentMetaAttribute = {}));
var NcmContentInstallType;
(function(NcmContentInstallType2) {
  NcmContentInstallType2[NcmContentInstallType2["Full"] = 0] = "Full";
  NcmContentInstallType2[NcmContentInstallType2["FragmentOnly"] = 1] = "FragmentOnly";
  NcmContentInstallType2[NcmContentInstallType2["Unknown"] = 7] = "Unknown";
})(NcmContentInstallType || (NcmContentInstallType = {}));
var NcmContentMetaPlatform;
(function(NcmContentMetaPlatform2) {
  NcmContentMetaPlatform2[NcmContentMetaPlatform2["Nx"] = 0] = "Nx";
})(NcmContentMetaPlatform || (NcmContentMetaPlatform = {}));
var NcmContentId = class _NcmContentId extends ArrayBufferStruct {
  //u8 c[0x10]; ///< Id
  static sizeof = 16;
  toString() {
    if (this.byteLength !== 16) {
      throw new Error("Content ID must be 16 bytes");
    }
    const v = view(this);
    return `${v.getBigUint64(0).toString(16).padStart(16, "0")}${v.getBigUint64(8).toString(16).padStart(16, "0")}`;
  }
  static from(s) {
    const lower = BigInt(`0x${s.slice(0, 16)}`);
    const upper = BigInt(`0x${s.slice(16, 32)}`);
    const id = new _NcmContentId();
    const v = view(id);
    v.setBigUint64(0, lower);
    v.setBigUint64(8, upper);
    return id;
  }
};
var NcmPlaceHolderId = class extends ArrayBufferStruct {
  //Uuid uuid;  ///< UUID
  static sizeof = 16;
};
var NcmContentStorageRecord = class extends ArrayBufferStruct {
  //NcmContentMetaKey key;
  //u8 storage_id;
  //u8 padding[0x7];
  static sizeof = 24;
  get key() {
    return contentMetaKeyCache(this);
  }
  set key(v) {
    u8(this).set(u8(v), 0);
  }
  get storageId() {
    return view(this).getUint8(16);
  }
  set storageId(v) {
    view(this).setUint8(16, v);
  }
};
var NcmContentMetaKey = class extends ArrayBufferStruct {
  //u64 id;                             ///< Id.
  //u32 version;                        ///< Version.
  //u8 type;                            ///< \ref NcmContentMetaType
  //u8 install_type;                    ///< \ref NcmContentInstallType
  //u8 padding[2];                      ///< Padding.
  static sizeof = 16;
  get id() {
    return view(this).getBigUint64(0, true);
  }
  set id(v) {
    view(this).setBigUint64(0, v, true);
  }
  get version() {
    return view(this).getUint32(8, true);
  }
  set version(v) {
    view(this).setUint32(8, v, true);
  }
  get type() {
    return view(this).getUint8(12);
  }
  set type(v) {
    view(this).setUint8(12, v);
  }
  get installType() {
    return view(this).getUint8(13);
  }
  set installType(v) {
    view(this).setUint8(13, v);
  }
};
var NcmContentInfo = class extends ArrayBufferStruct {
  //NcmContentId content_id;     ///< \ref NcmContentId
  //u32 size_low;                ///< Content size (low).
  //u8 size_high;                ///< Content size (high).
  //u8 attr;                     ///< Content attributes.
  //u8 content_type;             ///< \ref NcmContentType.
  //u8 id_offset;                ///< Offset of this content. Unused by most applications.
  static sizeof = 24;
  get contentId() {
    return contentIdCache(this);
  }
  set contentId(v) {
    u8(this).set(u8(v), 0);
  }
  get sizeLow() {
    return view(this).getUint32(16, true);
  }
  get sizeHigh() {
    return view(this).getUint8(20);
  }
  get size() {
    return BigInt(this.sizeHigh) << 32n | BigInt(this.sizeLow);
  }
  set size(v) {
    const dv = view(this);
    dv.setUint32(16, Number(v & 0xffffffffn), true);
    dv.setUint8(20, Number(v >> 32n));
  }
  get attr() {
    return view(this).getUint8(21);
  }
  set attr(v) {
    view(this).setUint8(21, v);
  }
  get contentType() {
    return view(this).getUint8(22);
  }
  set contentType(v) {
    view(this).setUint8(22, v);
  }
  get idOffset() {
    return view(this).getUint8(23);
  }
  set idOffset(v) {
    view(this).setUint8(23, v);
  }
};
var NcmPackagedContentInfo = class extends ArrayBufferStruct {
  //u8 hash[SHA256_HASH_SIZE];
  //NcmContentInfo info;
  static sizeof = 56;
  get hash() {
    return u8(this).subarray(0, 32);
  }
  set hash(v) {
    u8(this).set(v.subarray(0, 32), 0);
  }
  get contentInfo() {
    return new NcmContentInfo(this, 32);
  }
};
var NcmContentMetaHeader = class extends ArrayBufferStruct {
  // u16 extended_header_size;           ///< Size of optional struct that comes after this one.
  // u16 content_count;                  ///< Number of NcmContentInfos after the extra bytes.
  // u16 content_meta_count;             ///< Number of NcmContentMetaInfos that come after the NcmContentInfos.
  // u8 attributes;                      ///< Usually None (0).
  // u8 storage_id;                      ///< Usually None (0).
  static sizeof = 8;
  get extendedHeaderSize() {
    return view(this).getUint16(0, true);
  }
  set extendedHeaderSize(v) {
    view(this).setUint16(0, v, true);
  }
  get contentCount() {
    return view(this).getUint16(2, true);
  }
  set contentCount(v) {
    view(this).setUint16(2, v, true);
  }
  get contentMetaCount() {
    return view(this).getUint16(4, true);
  }
  set contentMetaCount(v) {
    view(this).setUint16(4, v, true);
  }
  get attributes() {
    return view(this).getUint8(6);
  }
  set attributes(v) {
    view(this).setUint8(6, v);
  }
  get storageId() {
    return view(this).getUint8(7);
  }
  set storageId(v) {
    view(this).setUint8(7, v);
  }
};
var NcmApplicationMetaExtendedHeader = class extends ArrayBufferStruct {
  // u64 patch_id;                     ///< PatchId of this application's patch.
  // u32 required_system_version;      ///< Firmware version required by this application.
  // u32 required_application_version; ///< [9.0.0+] Owner application version required by this application. Previously padding.
  static sizeof = 16;
  get patchId() {
    return view(this).getBigUint64(0, true);
  }
  set patchId(v) {
    view(this).setBigUint64(0, v, true);
  }
  get requiredSystemVersion() {
    return view(this).getUint32(8, true);
  }
  set requiredSystemVersion(v) {
    view(this).setUint32(8, v, true);
  }
  get requiredApplicationVersion() {
    return view(this).getUint32(12, true);
  }
  set requiredApplicationVersion(v) {
    view(this).setUint32(12, v, true);
  }
};
var NcmPatchMetaExtendedHeader = class extends ArrayBufferStruct {
  // u64 application_id;          ///< ApplicationId of this patch's corresponding application.
  // u32 required_system_version; ///< Firmware version required by this patch.
  // u32 extended_data_size;      ///< Size of the extended data following the NcmContentInfos.
  // u8 reserved[0x8];            ///< Unused.
  static sizeof = 24;
  get applicationId() {
    return view(this).getBigUint64(0, true);
  }
  set applicationId(v) {
    view(this).setBigUint64(0, v, true);
  }
  get requiredSystemVersion() {
    return view(this).getUint32(8, true);
  }
  set requiredSystemVersion(v) {
    view(this).setUint32(8, v, true);
  }
  get extendedDataSize() {
    return view(this).getUint32(12, true);
  }
  set extendedDataSize(v) {
    view(this).setUint32(12, v, true);
  }
};

// node_modules/.pnpm/@nx.js+ncm@1.2.0/node_modules/@nx.js/ncm/dist/content-storage.js
var NcmContentStorage = class _NcmContentStorage {
  static open(storageId) {
    const out = new Switch.Service();
    const inArr = new Uint8Array([storageId]);
    ncm.dispatchIn(4, inArr, {
      outObjects: [out]
    });
    return new _NcmContentStorage(out);
  }
  #srv;
  /**
   * @private
   */
  constructor(srv) {
    this.#srv = srv;
  }
  generatePlaceHolderId() {
    const uuid = new NcmPlaceHolderId();
    this.#srv.dispatchOut(0, uuid);
    return uuid;
  }
  createPlaceHolder(contentId, placeholderId, size) {
    const inData = new Uint8Array(40);
    inData.set(u8(placeholderId), 0);
    inData.set(u8(contentId), NcmPlaceHolderId.sizeof);
    view(inData).setBigInt64(32, size, true);
    this.#srv.dispatchIn(1, inData);
  }
  deletePlaceHolder(placeholderId) {
    this.#srv.dispatchIn(2, placeholderId);
  }
  hasPlaceHolder(placeholderId) {
    const out = new Uint8Array(1);
    this.#srv.dispatchInOut(3, placeholderId, out);
    return Boolean(out[0] & 1);
  }
  writePlaceHolder(placeholderId, offset, data) {
    const inData = new Uint8Array(24);
    inData.set(u8(placeholderId), 0);
    view(inData).setBigUint64(NcmPlaceHolderId.sizeof, offset, true);
    this.#srv.dispatchIn(4, inData, {
      bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.In],
      buffers: [data]
    });
  }
  register(contentId, placeholderId) {
    const inData = new Uint8Array(32);
    inData.set(u8(placeholderId), 0);
    inData.set(u8(contentId), NcmPlaceHolderId.sizeof);
    this.#srv.dispatchIn(5, inData);
  }
  delete(contentId) {
    this.#srv.dispatchIn(6, contentId);
  }
  has(contentId) {
    const out = new Uint8Array(1);
    this.#srv.dispatchInOut(7, contentId, out);
    return Boolean(out[0] & 1);
  }
  getPath(contentId) {
    const out = new Uint8Array(768);
    this.#srv.dispatchIn(8, contentId, {
      bufferAttrs: [
        SfBufferAttr.FixedSize | SfBufferAttr.HipcPointer | SfBufferAttr.Out
      ],
      buffers: [out]
    });
    return decodeCString(out);
  }
  getPlaceHolderPath(placeholderId) {
    const out = new Uint8Array(768);
    this.#srv.dispatchIn(9, placeholderId, {
      bufferAttrs: [
        SfBufferAttr.FixedSize | SfBufferAttr.HipcPointer | SfBufferAttr.Out
      ],
      buffers: [out]
    });
    return decodeCString(out);
  }
  cleanupAllPlaceHolder() {
    this.#srv.dispatch(10);
  }
  listPlaceHolder(maxCount = 20) {
    const outIds = new ArrayBuffer(maxCount * NcmPlaceHolderId.sizeof);
    const out = new Int32Array(1);
    this.#srv.dispatchOut(11, out, {
      bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.Out],
      buffers: [outIds]
    });
    const outCount = out[0];
    const rtn = new Array(outCount);
    for (let i = 0; i < outCount; i++) {
      rtn[i] = new NcmPlaceHolderId(outIds, i * NcmPlaceHolderId.sizeof);
    }
    return rtn;
  }
  getContentCount() {
    const out = new Int32Array(1);
    this.#srv.dispatchOut(12, out);
    return out[0];
  }
  listContentId(startOffset = 0, maxCount = this.getContentCount()) {
    const inData = new Int32Array([startOffset]);
    const outIds = new ArrayBuffer(maxCount * 16);
    const out = new Int32Array(1);
    this.#srv.dispatchInOut(13, inData, out, {
      bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.Out],
      buffers: [outIds]
    });
    const outCount = out[0];
    const rtn = new Array(outCount);
    for (let i = 0; i < outCount; i++) {
      rtn[i] = new NcmContentId(outIds, i * 16);
    }
    return rtn;
  }
  getSizeFromContentId(contentId) {
    const out = new BigInt64Array(1);
    this.#srv.dispatchInOut(14, contentId, out);
    return out[0];
  }
  disableForcibly() {
    this.#srv.dispatch(15);
  }
  get freeSpace() {
    const out = new BigInt64Array(1);
    this.#srv.dispatchOut(22, out);
    return out[0];
  }
  get totalSpace() {
    const out = new BigInt64Array(1);
    this.#srv.dispatchOut(23, out);
    return out[0];
  }
  flushPlaceHolder() {
    this.#srv.dispatch(24);
  }
  getSizeFromPlaceHolderId(placeholderId) {
    const out = new BigInt64Array(1);
    this.#srv.dispatchInOut(25, placeholderId, out);
    return out[0];
  }
  repairInvalidFileAttribute() {
    this.#srv.dispatch(26);
  }
};

// node_modules/.pnpm/@nx.js+ncm@1.2.0/node_modules/@nx.js/ncm/dist/content-meta-database.js
var NcmContentMetaDatabase = class _NcmContentMetaDatabase {
  static open(storageId) {
    const out = new Switch.Service();
    const inArr = new Uint8Array([storageId]);
    ncm.dispatchIn(5, inArr, {
      outObjects: [out]
    });
    return new _NcmContentMetaDatabase(out);
  }
  #srv;
  /**
   * @private
   */
  constructor(srv) {
    this.#srv = srv;
  }
  set(key, data) {
    this.#srv.dispatchIn(0, key, {
      bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.In],
      buffers: [data]
    });
  }
  getInto(key, data) {
    const out = new BigUint64Array(1);
    this.#srv.dispatchInOut(1, key, out.buffer, {
      bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.Out],
      buffers: [data]
    });
    return out[0];
  }
  get(key) {
    const expectedSize = this.getSize(key);
    const data = new Uint8Array(Number(expectedSize));
    const size = this.getInto(key, data);
    if (size !== expectedSize) {
      throw new Error(`Unexpected size: ${size} (expected: ${expectedSize})`);
    }
    return data;
  }
  delete(key) {
    this.#srv.dispatchIn(2, key);
  }
  getContentIdByType(key, type) {
    const inData = new Uint8Array(24);
    inData[0] = type;
    inData.set(u8(key), 8);
    const out = new NcmContentId();
    this.#srv.dispatchInOut(3, inData, out);
    return out;
  }
  listContentInfo(key, startIndex, outInfo) {
    const inData = new Uint8Array(24);
    const inView = view(inData);
    inView.setInt32(0, startIndex ?? 0, true);
    inView.setUint32(4, 0, true);
    inData.set(u8(key), 8);
    const out = new Int32Array(1);
    this.#srv.dispatchInOut(4, inData, out, {
      bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.Out],
      buffers: [outInfo]
    });
    return out[0];
  }
  has(key) {
    const out = new Uint8Array(1);
    this.#srv.dispatchInOut(8, key, out);
    return Boolean(out[0] & 1);
  }
  hasAll(keys) {
    const inData = new Uint8Array(keys.length * NcmContentMetaKey.sizeof);
    for (let i = 0; i < keys.length; i++) {
      inData.set(u8(keys[i]), i * 16);
    }
    const out = new Uint8Array(1);
    this.#srv.dispatchOut(9, out, {
      bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.In],
      buffers: [inData]
    });
    return Boolean(out[0] & 1);
  }
  getSize(key) {
    const out = new BigUint64Array(1);
    this.#srv.dispatchInOut(10, key, out);
    return out[0];
  }
  commit() {
    this.#srv.dispatch(15);
  }
};

// node_modules/.pnpm/@nx.js+ns@1.0.0/node_modules/@nx.js/ns/dist/types.js
var NsApplicationRecordType;
(function(NsApplicationRecordType2) {
  NsApplicationRecordType2[NsApplicationRecordType2["Installed"] = 3] = "Installed";
  NsApplicationRecordType2[NsApplicationRecordType2["GamecardMissing"] = 5] = "GamecardMissing";
  NsApplicationRecordType2[NsApplicationRecordType2["Archived"] = 11] = "Archived";
})(NsApplicationRecordType || (NsApplicationRecordType = {}));

// node_modules/.pnpm/@nx.js+ns@1.0.0/node_modules/@nx.js/ns/dist/index.js
var nsAm2 = new Switch.Service("ns:am2");
var nsAppManSrv = new Switch.Service();
nsAm2.dispatch(7996, {
  outObjects: [nsAppManSrv]
});
function nsPushApplicationRecord(titleId, lastModifiedEvent, contentRecords) {
  const inData = new ArrayBuffer(16);
  new Uint8Array(inData)[0] = lastModifiedEvent;
  new BigUint64Array(inData, 8)[0] = titleId;
  nsAppManSrv.dispatchIn(16, inData, {
    bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.In],
    buffers: [contentRecords]
  });
}
function nsListApplicationRecordContentMeta(offset, titleId, outBuf) {
  const inData = new BigUint64Array([offset, titleId]);
  const out = new Uint32Array(1);
  nsAppManSrv.dispatchInOut(17, inData, out, {
    bufferAttrs: [SfBufferAttr.HipcMapAlias | SfBufferAttr.Out],
    buffers: [outBuf]
  });
  return out[0];
}
function nsDeleteApplicationRecord(titleId) {
  const inData = new BigUint64Array([titleId]);
  nsAppManSrv.dispatchIn(27, inData);
}
function nsInvalidateApplicationControlCache(titleId) {
  const inData = new BigUint64Array([titleId]);
  nsAppManSrv.dispatchIn(404, inData);
}

// node_modules/.pnpm/@tootallnate+nsp@0.0.2/node_modules/@tootallnate/nsp/dist/index.js
var SIZEOF_HEADER = 16;
var SIZEOF_FILE_ENTRY = 24;
var decoder = new TextDecoder();
async function parseNsp(blob) {
  const s = blob.slice(0, SIZEOF_HEADER);
  const buf = await s.arrayBuffer();
  const view2 = new DataView(buf);
  const magic = view2.getUint32(0, true);
  if (magic !== 810763856) {
    throw new Error("Not a PFS0 file");
  }
  const fileCount = view2.getUint32(4, true);
  const stringTableSize = view2.getUint32(8, true);
  const reserved = view2.getUint32(12, true);
  if (reserved !== 0) {
    throw new Error("Reserved must be 0");
  }
  const stringTableOffset = SIZEOF_HEADER + fileCount * SIZEOF_FILE_ENTRY;
  const stringTableBlob = blob.slice(stringTableOffset, stringTableOffset + stringTableSize);
  const stringTableData = new Uint8Array(await stringTableBlob.arrayBuffer());
  const files = /* @__PURE__ */ new Map();
  const fileDataOffset = stringTableOffset + stringTableSize;
  for (let i = 0; i < fileCount; i++) {
    const offset = SIZEOF_HEADER + i * SIZEOF_FILE_ENTRY;
    const data = await blob.slice(offset, offset + SIZEOF_FILE_ENTRY).arrayBuffer();
    const view3 = new DataView(data);
    const fileEntry = {
      offset: view3.getBigUint64(0, true),
      size: view3.getBigUint64(8, true),
      stringTableOffset: view3.getUint32(16, true),
      pad: view3.getUint32(20, true)
    };
    let nameEndOffset = 0;
    for (let j = fileEntry.stringTableOffset; j < stringTableSize; j++) {
      const ch = stringTableData[j];
      if (ch === 0) {
        nameEndOffset = j;
        break;
      }
    }
    const name = decoder.decode(stringTableData.subarray(fileEntry.stringTableOffset, nameEndOffset));
    const fileContents = blob.slice(fileDataOffset + Number(fileEntry.offset), fileDataOffset + Number(fileEntry.offset + fileEntry.size));
    files.set(name, {
      offset: fileEntry.offset,
      size: fileEntry.size,
      stringTableOffset: fileEntry.stringTableOffset,
      pad: fileEntry.pad,
      data: fileContents
    });
  }
  return {
    fileCount,
    stringTableSize,
    reserved,
    files
  };
}

// node_modules/.pnpm/@nx.js+install-title@0.0.3/node_modules/@nx.js/install-title/dist/ipc/es.js
var es = new Switch.Service("es");
function esImportTicket(tik, cert) {
  es.dispatch(1, {
    bufferAttrs: [
      SfBufferAttr.HipcMapAlias | SfBufferAttr.In,
      SfBufferAttr.HipcMapAlias | SfBufferAttr.In
    ],
    buffers: [tik, cert]
  });
}

// node_modules/.pnpm/@nx.js+install-title@0.0.3/node_modules/@nx.js/install-title/dist/types.js
var PackagedContentMetaHeader = class extends ArrayBufferStruct {
  //u64 title_id;
  //u32 version;
  //u8 type; /* NcmContentMetaType */
  //u8 _0xd;
  //u16 extended_header_size;
  //u16 content_count;
  //u16 content_meta_count;
  //u8 attributes;
  //u8 storage_id;
  //u8 install_type; /* NcmContentInstallType */
  //bool comitted;
  //u32 required_system_version;
  //u32 _0x1c;
  static sizeof = 32;
  get titleId() {
    return view(this).getBigUint64(0, true);
  }
  get version() {
    return view(this).getUint32(8, true);
  }
  get type() {
    return view(this).getUint8(12);
  }
  get extendedHeaderSize() {
    return view(this).getUint16(14, true);
  }
  get contentCount() {
    return view(this).getUint16(16, true);
  }
};

// node_modules/.pnpm/@nx.js+install-title@0.0.3/node_modules/@nx.js/install-title/dist/index.js
async function* step(name, fn) {
  const timeStart = performance.now();
  yield { type: "start", name, timeStart };
  try {
    return yield* fn();
  } finally {
    yield { type: "end", name, timeStart, timeEnd: performance.now() };
  }
}
async function* installNsp(data, storageId) {
  const contentStorage = NcmContentStorage.open(storageId);
  const contentMetaDatabase = NcmContentMetaDatabase.open(storageId);
  const ncaFiles = /* @__PURE__ */ new Set();
  const titleIdToContentMetaKeysMap = /* @__PURE__ */ new Map();
  const nsp = yield* step("Parse NSP", async function* () {
    return parseNsp(data);
  });
  const cnmtNcaFiles = nsp.files.entries().filter(([name]) => name.endsWith(".cnmt.nca"));
  for (const [name, entry] of cnmtNcaFiles) {
    await installNca(name, entry, contentStorage);
    const cnmtData = await readContentMeta(name, contentStorage);
    const contentMetaKey = await installContentMetaRecords(contentMetaDatabase, cnmtData, createMetaContentInfo(name, entry), ncaFiles);
    const cnmtHeader = new PackagedContentMetaHeader(cnmtData);
    const titleId = getBaseTitleId(cnmtHeader.titleId, cnmtHeader.type);
    let contentMetaKeys = titleIdToContentMetaKeysMap.get(titleId);
    if (!contentMetaKeys) {
      contentMetaKeys = [];
      titleIdToContentMetaKeysMap.set(titleId, contentMetaKeys);
    }
    contentMetaKeys.push(contentMetaKey);
  }
  yield* importTicketCerts(nsp.files);
  for (const name of ncaFiles) {
    const entry = nsp.files.get(name);
    if (!entry) {
      throw new Error(`Missing "${name}" file`);
    }
    await installNca(name, entry, contentStorage);
  }
  for (const [titleId, contentMetaKeys] of titleIdToContentMetaKeysMap) {
    await pushApplicationRecord(titleId, contentMetaKeys, storageId);
  }
}
var ContentStoragePlaceholderWriteStream = class extends WritableStream {
  #offset;
  constructor(contentStorage, contentId, size) {
    const placeholderId = contentStorage.generatePlaceHolderId();
    try {
      contentStorage.deletePlaceHolder(placeholderId);
    } catch {
    }
    contentStorage.createPlaceHolder(contentId, placeholderId, BigInt(size));
    super({
      write: (chunk) => {
        console.debug(`Writing ${chunk.byteLength} bytes (${this.#offset})`);
        contentStorage.writePlaceHolder(placeholderId, this.#offset, chunk);
        this.#offset += BigInt(chunk.byteLength);
      },
      close: () => {
        console.debug(`Finished writing ${this.#offset} bytes`);
        try {
          contentStorage.delete(contentId);
        } catch {
        }
        contentStorage.register(contentId, placeholderId);
      }
    });
    this.#offset = 0n;
  }
};
function createContentMetaKey(header) {
  const key = new NcmContentMetaKey();
  key.id = header.titleId;
  key.version = header.version;
  key.type = header.type;
  key.installType = NcmContentInstallType.Full;
  return key;
}
function createMetaContentInfo(name, entry) {
  const info = new NcmContentInfo();
  info.contentId = NcmContentId.from(name);
  info.size = entry.size;
  info.contentType = NcmContentType.Meta;
  return info;
}
async function installNca(name, entry, contentStorage) {
  console.debug(`Installing "${name}" (${entry.size} bytes)`);
  const contentId = NcmContentId.from(name);
  await entry.data.stream().pipeTo(new ContentStoragePlaceholderWriteStream(contentStorage, contentId, entry.size));
}
async function* importTicketCerts(files) {
  const ticketFiles = files.keys().filter((name) => name.endsWith(".tik"));
  for (const tikName of ticketFiles) {
    yield* step(`Importing ticket "${tikName}"`, async function* () {
      const hash = tikName.slice(0, -4);
      const tikEntry = files.get(tikName);
      if (!tikEntry) {
        throw new Error(`Missing "${tikName}" file`);
      }
      const certName = `${hash}.cert`;
      const certEntry = files.get(certName);
      if (!certEntry) {
        throw new Error(`Missing "${certName}" file`);
      }
      const tik = await tikEntry.data.arrayBuffer();
      const cert = await certEntry.data.arrayBuffer();
      console.debug(`Importing "${tikName}" and "${certName}" certificate`);
      esImportTicket(tik, cert);
    });
  }
}
async function readContentMeta(name, contentStorage) {
  const contentId = NcmContentId.from(name);
  const path = contentStorage.getPath(contentId);
  console.debug(`Mounting path "${path}"`);
  const fs = Switch.FileSystem.openWithId(0n, FsFileSystemType.ContentMeta, path);
  const url = fs.mount();
  const files = Switch.readDirSync(url);
  if (!files) {
    throw new Error("Failed to read directory");
  }
  const cnmtName = files.find((name2) => name2.endsWith(".cnmt"));
  if (!cnmtName) {
    throw new Error('Failed to find ".cnmt" file');
  }
  console.debug(`Reading "${cnmtName}" file`);
  const cnmt = Switch.readFileSync(new URL(cnmtName, url));
  if (!cnmt) {
    throw new Error('Failed to read ".cnmt" file');
  }
  return cnmt;
}
async function installContentMetaRecords(contentMetaDatabase, cnmtData, cnmtContentInfo, ncaFilesToInstall) {
  const contentInfos = [];
  const cnmtHeader = new PackagedContentMetaHeader(cnmtData);
  const key = createContentMetaKey(cnmtHeader);
  const extendedHeaderSize = cnmtHeader.extendedHeaderSize;
  const type = cnmtHeader.type;
  let extendedDataSize = 0;
  contentInfos.push(cnmtContentInfo);
  const contentCount = cnmtHeader.contentCount;
  for (let i = 0; i < contentCount; i++) {
    const packagedContentInfoOffset = PackagedContentMetaHeader.sizeof + extendedHeaderSize + i * NcmPackagedContentInfo.sizeof;
    const packagedContentInfo = new NcmPackagedContentInfo(cnmtData, packagedContentInfoOffset);
    const contentInfo = packagedContentInfo.contentInfo;
    if (contentInfo.contentType === NcmContentType.DeltaFragment) {
      continue;
    }
    contentInfos.push(contentInfo);
    ncaFilesToInstall.add(`${contentInfo.contentId}.nca`);
  }
  if (type === NcmContentMetaType.Patch) {
    const patchMetaExtendedHeader = new NcmPatchMetaExtendedHeader(cnmtData, PackagedContentMetaHeader.sizeof);
    extendedDataSize = patchMetaExtendedHeader.extendedDataSize;
  }
  const contentMetaData = new Uint8Array(NcmContentMetaHeader.sizeof + extendedHeaderSize + contentInfos.length * NcmContentInfo.sizeof + extendedDataSize);
  const contentMetaHeader = new NcmContentMetaHeader(contentMetaData);
  contentMetaHeader.extendedHeaderSize = extendedHeaderSize;
  contentMetaHeader.contentCount = contentInfos.length;
  contentMetaHeader.contentMetaCount = 0;
  contentMetaHeader.attributes = 0;
  contentMetaHeader.storageId = 0;
  contentMetaData.set(new Uint8Array(cnmtData, PackagedContentMetaHeader.sizeof, extendedHeaderSize), NcmContentMetaHeader.sizeof);
  if (type === NcmContentMetaType.Application) {
    const extendedHeader = new NcmApplicationMetaExtendedHeader(contentMetaData, NcmContentMetaHeader.sizeof);
    extendedHeader.requiredSystemVersion = 0;
  } else if (type === NcmContentMetaType.Patch) {
    const extendedHeader = new NcmPatchMetaExtendedHeader(contentMetaData, NcmContentMetaHeader.sizeof);
    extendedHeader.requiredSystemVersion = 0;
  }
  for (let i = 0; i < contentInfos.length; i++) {
    const offset = NcmContentMetaHeader.sizeof + extendedHeaderSize + i * NcmContentInfo.sizeof;
    contentMetaData.set(u8(contentInfos[i]), offset);
  }
  if (extendedDataSize > 0) {
    contentMetaData.set(new Uint8Array(cnmtData, PackagedContentMetaHeader.sizeof + extendedHeaderSize + contentInfos.length * NcmContentInfo.sizeof, extendedDataSize), NcmContentMetaHeader.sizeof + extendedHeaderSize + contentInfos.length * NcmContentInfo.sizeof);
  }
  contentMetaDatabase.set(key, contentMetaData);
  contentMetaDatabase.commit();
  return key;
}
async function pushApplicationRecord(titleId, contentMetaKeys, storageId) {
  console.debug(`Pushing application record for "${titleId.toString(16).padStart(16, "0")}" (${contentMetaKeys.length} content meta keys)`);
  const contentStorageRecords = new Uint8Array(NcmContentStorageRecord.sizeof * 20);
  let entriesRead = 0;
  try {
    entriesRead = nsListApplicationRecordContentMeta(0n, titleId, contentStorageRecords);
    console.debug(`Found ${entriesRead} existing content meta records`);
  } catch (err) {
  }
  for (let i = 0; i < contentMetaKeys.length; i++) {
    const contentStorageRecord = new NcmContentStorageRecord(contentStorageRecords, (entriesRead + i) * NcmContentStorageRecord.sizeof);
    contentStorageRecord.key = contentMetaKeys[i];
    contentStorageRecord.storageId = storageId;
  }
  try {
    nsDeleteApplicationRecord(titleId);
  } catch {
  }
  nsPushApplicationRecord(titleId, NsApplicationRecordType.Installed, contentStorageRecords.slice(0, (entriesRead + contentMetaKeys.length) * NcmContentStorageRecord.sizeof));
  if (entriesRead > 0) {
    nsInvalidateApplicationControlCache(titleId);
  }
}
function getBaseTitleId(titleId, type) {
  switch (type) {
    case NcmContentMetaType.Patch:
      return titleId ^ 0x800n;
    case NcmContentMetaType.AddOnContent:
      return (titleId ^ 0x1000n) & ~0xfffn;
    default:
      return titleId;
  }
}

// node_modules/.pnpm/@tootallnate+nacp@0.2.0/node_modules/@tootallnate/nacp/dist/index.js
var __classPrivateFieldSet = function(receiver, state, value, kind, f) {
  if (kind === "m") throw new TypeError("Private method is not writable");
  if (kind === "a" && !f) throw new TypeError("Private accessor was defined without a setter");
  if (typeof state === "function" ? receiver !== state || !f : !state.has(receiver)) throw new TypeError("Cannot write private member to an object whose class did not declare it");
  return kind === "a" ? f.call(receiver, value) : f ? f.value = value : state.set(receiver, value), value;
};
var __classPrivateFieldGet = function(receiver, state, kind, f) {
  if (kind === "a" && !f) throw new TypeError("Private accessor was defined without a getter");
  if (typeof state === "function" ? receiver !== state || !f : !state.has(receiver)) throw new TypeError("Cannot read private member from an object whose class did not declare it");
  return kind === "m" ? f : kind === "a" ? f.call(receiver) : f ? f.value : state.get(receiver);
};
var _NACP_dataView;
var encoder = new TextEncoder();
var decoder2 = new TextDecoder();
var VideoCapture;
(function(VideoCapture2) {
  VideoCapture2[VideoCapture2["Disabled"] = 0] = "Disabled";
  VideoCapture2[VideoCapture2["Enabled"] = 1] = "Enabled";
  VideoCapture2[VideoCapture2["Automatic"] = 2] = "Automatic";
})(VideoCapture || (VideoCapture = {}));
function encodeWithSize(v, size, name) {
  const buf = encoder.encode(v);
  if (buf.length >= size) {
    throw new TypeError(`"${name}" length must be <= ${size - 1} bytes, got ${buf.length}`);
  }
  const bufWithZeros = new Uint8Array(size);
  bufWithZeros.set(buf);
  return bufWithZeros;
}
function parseU64(v) {
  let id = v;
  if (typeof id === "string") {
    if (id.length > 16) {
      throw new TypeError(`"id" length must be 16, got ${id.length}`);
    }
    id = BigInt(`0x${v}`);
  }
  return id;
}
var NACP = class {
  constructor(buffer) {
    _NACP_dataView.set(this, void 0);
    if (buffer) {
      this.buffer = buffer;
      __classPrivateFieldSet(this, _NACP_dataView, new DataView(this.buffer), "f");
    } else {
      this.buffer = new ArrayBuffer(16384);
      __classPrivateFieldSet(this, _NACP_dataView, new DataView(this.buffer), "f");
      __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12324, 256, true);
      __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12332, 3071, true);
      __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12340, 65536, true);
      const unkData = new Uint8Array([
        12,
        255,
        255,
        10,
        255,
        12,
        12,
        12,
        12,
        12,
        13,
        13,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255
      ]);
      new Uint8Array(this.buffer, 12352, unkData.length).set(unkData);
      __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12416, 65011712, true);
      __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12424, 1572864, true);
      __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12528, 258, true);
    }
  }
  get title() {
    const data = new Uint8Array(this.buffer, 0, 512);
    return decoder2.decode(data).replace(/\0*$/, "");
  }
  set title(v) {
    const buf = encodeWithSize(v, 512, "title");
    for (let i = 0; i < 12; i++) {
      new Uint8Array(this.buffer, i * 768, buf.length).set(buf);
    }
  }
  get author() {
    const data = new Uint8Array(this.buffer, 512, 256);
    return decoder2.decode(data).replace(/\0*$/, "");
  }
  set author(v) {
    const buf = encodeWithSize(v, 256, "author");
    for (let i = 0; i < 12; i++) {
      new Uint8Array(this.buffer, i * 768 + 512, buf.length).set(buf);
    }
  }
  set id(v) {
    const val = parseU64(v);
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12344, val, true);
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12408, val, true);
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12400, val + 0x1000n, true);
    for (let x = 0; x < 8; x++) {
      __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12464 + x * 8, val, true);
    }
  }
  get id() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12344, true);
  }
  /**
   * Whether or not to display the user account picker
   * when booting up the application.
   */
  set startupUserAccount(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12325, v);
  }
  get startupUserAccount() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12325);
  }
  set userAccountSwitchLock(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12326, v);
  }
  get userAccountSwitchLock() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12326);
  }
  set addOnContentRegistrationType(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12327, v);
  }
  get addOnContentRegistrationType() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12327);
  }
  set attributeFlag(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12328, v, true);
  }
  get attributeFlag() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint32(12328, true);
  }
  set supportedLanguageFlag(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12332, v, true);
  }
  get supportedLanguageFlag() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint32(12332, true);
  }
  set parentalControlFlag(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint32(12336, v, true);
  }
  get parentalControlFlag() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint32(12336, true);
  }
  /**
   * Whether or not pressing the screenshot button will capture a screenshot.
   *   - Value of `0`: Enabled
   *   - Value of `1`: Disabled
   */
  set screenshot(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12340, v);
  }
  get screenshot() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12340);
  }
  /**
   * Whether or not holding the screenshot button will capture a video.
   *   - Value of `0`: Disabled
   *   - Value of `1`: Only enabled if app invokes `appletInitializeGamePlayRecording()`
   *   - Value of `2`: Always enabled
   */
  set videoCapture(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12341, v);
  }
  get videoCapture() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12341);
  }
  set dataLossConfirmation(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12342, v);
  }
  get dataLossConfirmation() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12342);
  }
  set playLogPolicy(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12343, v);
  }
  get playLogPolicy() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12343);
  }
  set presenceGroupId(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12344, parseU64(v), true);
  }
  get presenceGroupId() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12344, true);
  }
  get version() {
    const data = new Uint8Array(this.buffer, 12384, 16);
    return decoder2.decode(data).replace(/\0*$/, "");
  }
  set version(v) {
    const buf = encodeWithSize(v, 16, "version");
    new Uint8Array(this.buffer, 12384, buf.length).set(buf);
  }
  set addOnContentBaseId(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12400, parseU64(v), true);
  }
  get addOnContentBaseId() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12400, true);
  }
  set saveDataOwnerId(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12408, parseU64(v), true);
  }
  get saveDataOwnerId() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12408, true);
  }
  set userAccountSaveDataSize(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12416, parseU64(v), true);
  }
  get userAccountSaveDataSize() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12416, true);
  }
  set userAccountSaveDataJournalSize(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12424, parseU64(v), true);
  }
  get userAccountSaveDataJournalSize() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12424, true);
  }
  set deviceSaveDataSize(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12432, parseU64(v), true);
  }
  get deviceSaveDataSize() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12432, true);
  }
  set deviceSaveDataJournalSize(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12440, parseU64(v), true);
  }
  get deviceSaveDataJournalSize() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12440, true);
  }
  set bcatDeliveryCacheStorageSize(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12448, parseU64(v), true);
  }
  get bcatDeliveryCacheStorageSize() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12448, true);
  }
  set applicationErrorCodeCategory(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12456, parseU64(v), true);
  }
  get applicationErrorCodeCategory() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12456, true);
  }
  /**
   * Text shown above logo during boot-up.
   *   - Value of 0: "Licensed by"
   *   - Value of 1: "Distributed by"
   *   - Anything else: no text shown
   */
  set logoType(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12528, v);
  }
  get logoType() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12528);
  }
  set logoHandling(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12529, v);
  }
  get logoHandling() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12529);
  }
  set runtimeAddOnContentInstall(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12530, v);
  }
  get runtimeAddOnContentInstall() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12530);
  }
  set runtimeParameterDelivery(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12531, v);
  }
  get runtimeParameterDelivery() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12531);
  }
  set crashReport(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12534, v);
  }
  get crashReport() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12534);
  }
  set hdcp(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12535, v);
  }
  get hdcp() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12535);
  }
  set pseudoDeviceIdSeed(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12456, parseU64(v), true);
  }
  get pseudoDeviceIdSeed() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12456, true);
  }
  set startupUserAccountOption(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12609, v);
  }
  get startupUserAccountOption() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12609);
  }
  set userAccountSaveDataSizeMax(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12616, parseU64(v), true);
  }
  get userAccountSaveDataSizeMax() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12616, true);
  }
  set userAccountSaveDataJournalSizeMax(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12624, parseU64(v), true);
  }
  get userAccountSaveDataJournalSizeMax() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12624, true);
  }
  set deviceSaveDataSizeMax(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12632, parseU64(v), true);
  }
  get deviceSaveDataSizeMax() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12632, true);
  }
  set deviceSaveDataJournalSizeMax(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12640, parseU64(v), true);
  }
  get deviceSaveDataJournalSizeMax() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12640, true);
  }
  set temporaryStorageSize(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12648, parseU64(v), true);
  }
  get temporaryStorageSize() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12648, true);
  }
  set cacheStorageSize(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12656, parseU64(v), true);
  }
  get cacheStorageSize() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12656, true);
  }
  set cacheStorageJournalSize(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12664, parseU64(v), true);
  }
  get cacheStorageJournalSize() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12664, true);
  }
  set cacheStorageDataAndJournalSizeMax(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setBigUint64(12672, parseU64(v), true);
  }
  get cacheStorageDataAndJournalSizeMax() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getBigUint64(12672, true);
  }
  set cacheStorageIndexMax(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint16(12680, v, true);
  }
  get cacheStorageIndexMax() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint16(12680, true);
  }
  set playLogQueryCapability(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12816, v);
  }
  get playLogQueryCapability() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12816);
  }
  set repairFlag(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12817, v);
  }
  get repairFlag() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12817);
  }
  set programIndex(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12818, v);
  }
  get programIndex() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12818);
  }
  set requiredNetworkServiceLicenseOnLaunch(v) {
    __classPrivateFieldGet(this, _NACP_dataView, "f").setUint8(12819, v);
  }
  get requiredNetworkServiceLicenseOnLaunch() {
    return __classPrivateFieldGet(this, _NACP_dataView, "f").getUint8(12819);
  }
};
_NACP_dataView = /* @__PURE__ */ new WeakMap();

// src/installer_canvas.ts
function paintInstaller(ctx, width, height, state, background2) {
  const ru = state.language === "ru";
  const steps = ru ? ["\u041F\u043E\u0434\u0433\u043E\u0442\u043E\u0432\u043A\u0430 \u044F\u0440\u043B\u044B\u043A\u0430", "\u0421\u043E\u0437\u0434\u0430\u043D\u0438\u0435 \u043F\u0440\u0438\u043B\u043E\u0436\u0435\u043D\u0438\u044F", "\u0423\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0430 \u0432 HOME", "\u0412\u043E\u0437\u0432\u0440\u0430\u0449\u0430\u0435\u043C\u0441\u044F \u043A \u0438\u0433\u0440\u0435"] : ["Preparing shortcut", "Creating application", "Installing on HOME", "Returning to your game"];
  ctx.save();
  try {
    ctx.setTransform(width / 1280, 0, 0, height / 720, 0, 0);
    ctx.fillStyle = "#0a0e13";
    ctx.fillRect(0, 0, 1280, 720);
    if (background2) {
      try {
        ctx.drawImage(background2, 0, 0, 1280, 720);
      } catch {
      }
    }
    ctx.fillStyle = "rgba(5,9,14,0.52)";
    ctx.fillRect(0, 0, 1280, 720);
    ctx.fillStyle = "#152129";
    ctx.fillRect(315, 231, 650, 254);
    ctx.fillStyle = state.failed ? "#ee8a82" : "#54dc91";
    ctx.fillRect(348, 267, 5, 31);
    ctx.font = "27px system-ui";
    ctx.fillStyle = "#f2f6fa";
    ctx.fillText(state.failed ? ru ? "\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0441\u043E\u0437\u0434\u0430\u0442\u044C \u044F\u0440\u043B\u044B\u043A" : "Shortcut could not be created" : ru ? "\u042F\u0440\u043B\u044B\u043A \u043D\u0430 HOME" : "HOME shortcut", 370, 287, 565);
    ctx.font = "22px system-ui";
    ctx.fillStyle = "#c5d3dd";
    ctx.fillText(state.failed ? ru ? "\u0412\u043E\u0437\u0432\u0440\u0430\u0449\u0430\u0435\u043C\u0441\u044F \u043A \u0438\u0433\u0440\u0435\u2026" : "Returning to game\u2026" : steps[Math.max(0, Math.min(state.stage, 3))], 350, 345, 565);
    ctx.fillStyle = "#2b3e47";
    ctx.fillRect(350, 375, 580, 7);
    ctx.fillStyle = state.failed ? "#ee8a82" : "#54dc91";
    ctx.fillRect(350, 375, 145 * (Math.max(0, Math.min(state.stage, 3)) + 1), 7);
    ctx.font = "18px system-ui";
    ctx.fillStyle = "#8da5b3";
    ctx.fillText(ru ? "\u041F\u043E\u0441\u043B\u0435 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0438 \u0432\u044B \u0432\u0435\u0440\u043D\u0451\u0442\u0435\u0441\u044C \u043A \u0438\u0433\u0440\u0435." : "Your game page will return after installation.", 350, 433, 580);
    if (state.failed) ctx.fillText(ru ? "\u0415\u0441\u043B\u0438 \u0432\u043E\u0437\u0432\u0440\u0430\u0442 \u043D\u0435 \u0441\u0440\u0430\u0431\u043E\u0442\u0430\u043B: \u043D\u0430\u0436\u043C\u0438\u0442\u0435 +." : "If return fails: press +.", 350, 463, 580);
  } finally {
    ctx.restore();
  }
}

// src/preparation.ts
async function prepareVisibleInstaller(loadBackground2, nextFrame) {
  try {
    await loadBackground2();
  } catch {
  }
  await nextFrame();
  await nextFrame();
}

// src/ipc/spl.ts
var splMig = new Switch.Service("spl:mig");
function generateAesKek(wrappedKek, keyGeneration, option) {
  const input = new Uint8Array(24);
  input.set(new Uint8Array(wrappedKek, 0, 16), 0);
  const options = new Uint32Array(input.buffer, 16, 2);
  options[0] = keyGeneration;
  options[1] = option;
  const output = new Uint8Array(16);
  splMig.dispatchInOut(2, input.buffer, output.buffer);
  return output.buffer;
}
function generateAesKey(sealedKek, wrappedKey) {
  const input = new Uint8Array(32);
  input.set(new Uint8Array(sealedKek, 0, 16), 0);
  input.set(new Uint8Array(wrappedKey, 0, 16), 16);
  const output = new Uint8Array(16);
  splMig.dispatchInOut(4, input.buffer, output.buffer);
  return output.buffer;
}

// src/util.ts
function arrayBufferToHex(value) {
  return Array.from(new Uint8Array(value)).map((byte) => byte.toString(16).padStart(2, "0")).join("");
}
async function deterministicTitleId(nroPath, args) {
  const encoded = new TextEncoder().encode(nroPath + args);
  const digest = await crypto.subtle.digest("SHA-256", encoded);
  const hash64 = new DataView(digest).getBigUint64(0, true);
  const id = 0x0100000000000000n | hash64 & 0x00fffffffffff000n;
  return id.toString(16).padStart(16, "0");
}

// src/prod-keys.ts
function loadForwarderKeys() {
  const existing = Switch.readFileSync("sdmc:/switch/prod.keys");
  if (existing) return new Uint8Array(existing);
  const headerKekSource = new Uint8Array([
    31,
    18,
    145,
    58,
    74,
    203,
    240,
    13,
    76,
    222,
    58,
    246,
    213,
    35,
    136,
    42
  ]);
  const headerKeySource = new Uint8Array([
    90,
    62,
    216,
    79,
    222,
    192,
    216,
    38,
    49,
    247,
    226,
    93,
    25,
    123,
    245,
    208,
    28,
    155,
    123,
    250,
    246,
    40,
    24,
    61,
    113,
    246,
    77,
    115,
    241,
    80,
    185,
    210
  ]);
  const kek = generateAesKek(headerKekSource.buffer, 0, 0);
  const key0 = generateAesKey(kek, headerKeySource.buffer.slice(0, 16));
  const key1 = generateAesKey(kek, headerKeySource.buffer.slice(16));
  const keyHex = arrayBufferToHex(
    new Uint8Array([
      ...new Uint8Array(key0),
      ...new Uint8Array(key1)
    ]).buffer
  );
  return new TextEncoder().encode(
    `header_key = ${keyHex}
key_area_key_application_00 = 00000000000000000000000000000001
`
  );
}

// src/main.tsx
var requestPath = "sdmc:/switch/SwitchNOW/forwarder_request.json";
var resultPath = "sdmc:/switch/SwitchNOW/forwarder_result.json";
var exitEnabled = false;
var plusReleasedAfterInstall = false;
function waitForPaint() {
  return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}
var debugPath = "sdmc:/switch/SwitchNOW/forwarder_debug.log";
var debugLines = "";
function trace(event) {
  debugLines = (debugLines + `${(/* @__PURE__ */ new Date()).toISOString()} FORWARDER ${event}
`).slice(-24e3);
  try {
    Switch.writeFileSync(debugPath, debugLines);
  } catch {
  }
}
trace("installer_start R63");
var context = {};
try {
  const bytes = Switch.readFileSync("sdmc:/switch/SwitchNOW/.shortcut/game_context.json");
  context = bytes ? JSON.parse(new TextDecoder().decode(bytes)) : {};
  trace("context_loaded");
} catch {
  trace("context_unavailable");
}
var stage = 0;
var failed = false;
var background;
var painted = false;
var backgroundPainted = false;
var reportedFrameError = false;
var canvas = screen.getContext("2d");
if (!canvas) {
  trace("canvas_unavailable");
  throw new Error("Installer canvas unavailable");
}
async function loadBackground() {
  if (!context.preview_background) return;
  trace("background_read");
  try {
    const bytes = Switch.readFileSync(context.preview_background);
    if (!bytes || bytes.byteLength > 12 * 1024 * 1024) throw new Error("background size");
    background = await createImageBitmap(new Blob([bytes], { type: "image/png" }));
    trace(`background_ready ${background.width}x${background.height}`);
  } catch {
    trace("background_unavailable");
  }
}
function keepInstallerAlive() {
  try {
    paintInstaller(canvas, screen.width, screen.height, { language: context.language, stage, failed }, background);
    if (!painted) {
      painted = true;
      trace("first_frame_painted");
    }
    if (background && !backgroundPainted) {
      backgroundPainted = true;
      trace("background_frame_painted");
    }
    const rawButtons = navigator.getGamepads()[0]?.rawButtons ?? 0n;
    const plusPressed = (rawButtons & BigInt(HidNpadButton.Plus)) !== 0n;
    if (!exitEnabled) plusReleasedAfterInstall = false;
    else {
      if (!plusPressed) plusReleasedAfterInstall = true;
      if (plusReleasedAfterInstall && plusPressed) Switch.exit();
    }
  } catch {
    if (!reportedFrameError) {
      reportedFrameError = true;
      trace("paint_or_input_failed");
    }
  }
  requestAnimationFrame(keepInstallerAlive);
}
requestAnimationFrame(keepInstallerAlive);
function readRequest() {
  const data = Switch.readFileSync(requestPath);
  if (!data) throw new Error("SwitchNOW forwarder request is missing.");
  const request = JSON.parse(new TextDecoder().decode(data));
  if (request.version !== 1 || !request.name || !request.nroPath.startsWith("sdmc:/") || !request.returnPath?.startsWith("sdmc:/") || !request.returnPath.endsWith(".nro") || !request.args.includes(request.nroPath) || request.name.length > 511 || request.args.length > 1800) {
    throw new Error("SwitchNOW forwarder request is invalid.");
  }
  return request;
}
async function returnToSwitchNow(request) {
  trace("return_requested");
  await waitForPaint();
  await waitForPaint();
  return new Switch.Application(request.returnPath).launch(
    "--forwarder-result"
  );
}
async function generateNsp(request) {
  const wasm = Switch.readFileSync("romfs:/hacbrewpack.wasm");
  const main = Switch.readFileSync("romfs:/template/exefs/main");
  const mainNpdm = Switch.readFileSync("romfs:/template/exefs/main.npdm");
  if (!wasm || !main || !mainNpdm) {
    throw new Error("Forwarder generator resources are incomplete.");
  }
  const app = new Switch.Application(request.nroPath);
  if (!app.icon) throw new Error("The shortcut NRO has no embedded icon.");
  const icon = app.icon;
  const titleId = await deterministicTitleId(request.nroPath, request.args);
  const keys = loadForwarderKeys();
  const createModule = (await Promise.resolve().then(() => __toESM(require_hacbrewpack()))).default;
  return await new Promise((resolve, reject) => {
    let module;
    void createModule({
      noInitialRun: true,
      // Emscripten defaults to console.log/error. On Switch that activates the
      // text console over the canvas, producing a black screen with raw output.
      // Progress belongs to the game overlay; failures use the result channel.
      print: () => {
      },
      printErr: () => {
      },
      wasmBinary: wasm,
      preRun(instance) {
        module = instance;
        const { FS } = instance;
        FS.writeFile("/keys.dat", keys);
        FS.mkdir("/exefs");
        FS.writeFile("/exefs/main", new Uint8Array(main));
        FS.writeFile("/exefs/main.npdm", new Uint8Array(mainNpdm));
        const nacp = new NACP();
        nacp.id = titleId;
        nacp.title = request.name;
        nacp.author = request.author || "Blade-Punisher";
        nacp.version = request.displayVersion || "0.0.6";
        nacp.startupUserAccount = 0;
        nacp.userAccountSaveDataSize = 0n;
        nacp.userAccountSaveDataJournalSize = 0n;
        FS.mkdir("/control");
        FS.writeFile("/control/control.nacp", new Uint8Array(nacp.buffer));
        FS.writeFile("/control/icon_AmericanEnglish.dat", new Uint8Array(icon));
        FS.mkdir("/romfs");
        FS.writeFile("/romfs/nextArgv", new TextEncoder().encode(request.args));
        FS.writeFile(
          "/romfs/nextNroPath",
          new TextEncoder().encode(request.nroPath)
        );
      },
      onRuntimeInitialized() {
        try {
          const exitCode = module.callMain([
            "--nopatchnacplogo",
            "--titleid",
            titleId,
            "--nologo",
            "--plaintext"
          ]);
          if (exitCode !== 0) {
            reject(new Error(`Forwarder generator exited with code ${exitCode}.`));
            return;
          }
          const name = module.FS.readdir("/hacbrewpack_nsp").find((entry) => entry.endsWith(".nsp"));
          if (!name) throw new Error("Generated NSP could not be found.");
          resolve(module.FS.readFile(`/hacbrewpack_nsp/${name}`));
        } catch (error) {
          reject(error);
        }
      }
    }).catch(reject);
  });
}
function writeResult(result) {
  try {
    Switch.writeFileSync(resultPath, JSON.stringify(result));
    trace("result_written");
  } catch {
    trace("result_write_failed");
  }
}
async function install() {
  await waitForPaint();
  await waitForPaint();
  await prepareVisibleInstaller(loadBackground, waitForPaint);
  trace("visible_preparation_ready");
  if (Switch.appletType() !== AppletType.Application)
    throw new Error("Full-memory/application mode is required to install a forwarder.");
  const request = readRequest();
  trace("request_valid");
  stage = 1;
  await waitForPaint();
  await waitForPaint();
  trace("nsp_generate_begin");
  const nsp = await generateNsp(request);
  trace("nsp_generate_done");
  stage = 2;
  await waitForPaint();
  await waitForPaint();
  trace("home_install_begin");
  for await (const _event of installNsp(new Blob([nsp]), NcmStorageId.SdCard)) {
  }
  trace("home_install_done");
  writeResult({ version: 1, success: true, name: request.name, installedAt: Date.now(), automatic: !!request.automatic });
  stage = 3;
  await returnToSwitchNow(request);
}
void install().catch(async (error) => {
  trace("install_failed");
  const message = error instanceof Error ? error.message : String(error);
  let automatic = false;
  let name = "";
  try {
    const r = readRequest();
    automatic = !!r.automatic;
    name = r.name;
  } catch {
  }
  writeResult({ version: 1, success: false, name, error: message, automatic });
  failed = true;
  try {
    await returnToSwitchNow(readRequest());
  } catch {
    trace("return_failed");
    exitEnabled = true;
  }
});
