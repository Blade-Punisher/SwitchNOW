from pathlib import Path
from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
import struct,hashlib
from PIL import Image
root=Path('build/native-artwork-reference'); key=bytes.fromhex('11'*16+'22'*16);records={}
for p in root.glob('*.nca'):
 raw=p.read_bytes(); data=bytearray(raw)
 for sector in range(6):
  dec=Cipher(algorithms.AES(key),modes.XTS(sector.to_bytes(16,'big'))).decryptor()
  data[sector*512:(sector+1)*512]=dec.update(raw[sector*512:(sector+1)*512])+dec.finalize()
 assert data[0x200:0x204]==b'NCA3'
 t=data[0x205]; name={0:'program',1:'meta',2:'control'}[t]
 (root/(name+'.dec')).write_bytes(data);(root/(name+'.raw')).write_bytes(raw)
 records[t]=hashlib.sha256(raw).digest()[:16]+len(raw).to_bytes(5,'little')+bytes([0,{0:1,1:0,2:3}[t],0])
(root/'database.bin').write_bytes(struct.pack('<HHHBB',16,3,0,0,5)+bytes(16)+records[1]+records[0]+records[2])
print('Decrypted synthetic-key fixtures and generated metadata database')
image=Image.new('RGB',(256,256),(180,30,50))
image.paste((20,180,70),(128,0,256,256))
image.save(root/'new.jpg',quality=90)
