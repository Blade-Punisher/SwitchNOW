const fs = require('node:fs');
const assert = require('node:assert/strict');
const esbuild = require('../tools/switchnow-forwarder-installer/node_modules/esbuild');
const Module = require('node:module');
const m = new Module('preparation');
m._compile(esbuild.transformSync(fs.readFileSync('tools/switchnow-forwarder-installer/src/preparation.ts','utf8'),{loader:'ts',format:'cjs'}).code,'preparation.cjs');
(async()=>{
 let ready; let finished=false; const events=[]; const frames=[];
 const decode=new Promise(resolve=>{ready=resolve});
 const task=m.exports.prepareVisibleInstaller(async()=>{events.push('decode');await decode;events.push('decoded')},()=>new Promise(resolve=>{events.push('frame');frames.push(resolve)})).then(()=>{finished=true;events.push('generate')});
 await Promise.resolve();assert.deepEqual(events,['decode']);assert.equal(finished,false);
 ready();for(let i=0;i<4;i++)await Promise.resolve();assert.deepEqual(events,['decode','decoded','frame']);assert.equal(finished,false);
 frames.shift()();for(let i=0;i<4;i++)await Promise.resolve();assert.equal(frames.length,1);assert.equal(finished,false);
 frames.shift()();await task;assert.equal(finished,true);assert.deepEqual(events,['decode','decoded','frame','frame','generate']);
 let painted=0;
 await m.exports.prepareVisibleInstaller(async()=>{throw new Error('bad PNG')},async()=>{painted++});
 assert.equal(painted,2);
 console.log('Production preparation tests passed: delayed decode blocks generation, two frames required, broken background still progresses.');
})().catch(e=>{console.error(e);process.exitCode=1});
