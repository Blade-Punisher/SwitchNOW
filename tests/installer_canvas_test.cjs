const fs = require('node:fs');
const assert = require('node:assert/strict');
const esbuild = require('../tools/switchnow-forwarder-installer/node_modules/esbuild');
const Module = require('node:module');
const transformed = esbuild.transformSync(fs.readFileSync('tools/switchnow-forwarder-installer/src/installer_canvas.ts','utf8'), {loader:'ts',format:'cjs'}).code;
const m = new Module('installer_canvas'); m._compile(transformed,'installer_canvas.cjs');
for (const language of ['ru','en']) for (const stage of [-1,0,1,2,3,99]) for (const failed of [false,true]) for (const imageState of ['absent','valid','broken']) {
 const texts=[],rects=[]; let balance=0;
 const ctx={save(){balance++},restore(){balance--},setTransform(...a){assert.deepEqual(a,[1,0,0,1,0,0])},fillRect(...a){rects.push(a)},fillText(s){texts.push(s)},drawImage(){if(imageState==='broken')throw new Error('decode failure')}};
 m.exports.paintInstaller(ctx,1280,720,{language,stage,failed},imageState==='absent'?undefined:{});
 assert.equal(balance,0);assert(texts.length>=3);assert(rects.some(r=>r[0]===315&&r[2]===650));
 assert(rects.at(-1)[2]>0 && rects.at(-1)[2]<=580);
 assert(texts[0].includes(language==='ru'?(failed?'Не удалось':'Ярлык'):(failed?'could not':'HOME')));
}
console.log('Production Canvas installer tests passed: 72 states, missing/broken background, language, progress bounds, balanced canvas.');
