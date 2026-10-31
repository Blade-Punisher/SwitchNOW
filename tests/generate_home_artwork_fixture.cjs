const fs=require('fs'), path=require('path');
const dir=path.resolve('build/native-artwork-reference');
fs.mkdirSync(dir,{recursive:true});
require(path.resolve('tools/switchnow-forwarder-installer/node_modules/esbuild')).buildSync({entryPoints:['tools/switchnow-forwarder-installer/src/hacbrewpack.js'],outfile:path.join(dir,'generator.cjs'),format:'cjs',platform:'node'});
global.window={};
const create=require(path.join(dir,'generator.cjs'));
(async()=>{
 const module=await create({noInitialRun:true,wasmBinary:fs.readFileSync('tools/switchnow-forwarder-installer/romfs/hacbrewpack.wasm'),print:()=>{},printErr:s=>{process.stderr.write(s+'\n')},preRun(m){
 const F=m.FS;
 // Synthetic test keys only. These never enter the app/release package.
 F.writeFile('/keys.dat',Buffer.from('header_key = '+'11'.repeat(16)+'22'.repeat(16)+'\nkey_area_key_application_00 = '+'00'.repeat(15)+'01\n'));
 F.mkdir('/exefs');for(const name of ['main','main.npdm'])F.writeFile('/exefs/'+name,fs.readFileSync('tools/switchnow-forwarder-installer/romfs/template/exefs/'+name));
 F.mkdir('/control');const nacp=Buffer.alloc(0x4000);for(let i=0;i<16;i++){nacp.write('Artwork Test',i*0x300);nacp.write('SwitchNOW',i*0x300+0x200)}F.writeFile('/control/control.nacp',nacp);F.writeFile('/control/icon_AmericanEnglish.dat',fs.readFileSync('resources/icon/icon.jpg'));
 F.mkdir('/romfs');F.writeFile('/romfs/nextNroPath',Buffer.from('sdmc:/switch/test.nro'));F.writeFile('/romfs/nextArgv',Buffer.from('test'));
 }});
 const code=module.callMain(['--nopatchnacplogo','--titleid','0100000000001000','--nologo','--plaintext']);if(code)throw Error('fixture generation failed '+code);
 const name=module.FS.readdir('/hacbrewpack_nsp').find(n=>n.endsWith('.nsp'));
 const nsp=Buffer.from(module.FS.readFile('/hacbrewpack_nsp/'+name));
 const count=nsp.readUInt32LE(4),strSize=nsp.readUInt32LE(8),strBase=16+count*24,base=strBase+strSize;
 for(let i=0;i<count;i++){const off=16+i*24,start=base+Number(nsp.readBigUInt64LE(off)),size=Number(nsp.readBigUInt64LE(off+8)),nameStart=strBase+nsp.readUInt32LE(off+16);const file=nsp.toString('utf8',nameStart,nsp.indexOf(0,nameStart)); if(file.endsWith('.nca')) fs.writeFileSync(path.join(dir,file),nsp.subarray(start,start+size));}
 console.log('Generated real forwarder NCA fixtures');
})().catch(e=>{console.error(e.stack,e.errno,e.node?.name);process.exit(1)});



