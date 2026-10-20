// Rasterize the selected Markdown + Mermaid launcher for Windows toolbars.
// npm install --no-save sharp, or set NODE_PATH to a runtime containing sharp.
const fs = require('node:fs');
const sharp = require('sharp');
const folder = 'assets/icons';
const source = folder + '/markdown-mermaid.svg';
async function rgba(size) { return sharp(source, { density: 192 }).resize(size, size).ensureAlpha().raw().toBuffer(); }
function dib(pixels, size) {
  const maskStride = Math.ceil(size / 32) * 4, colorSize = size * size * 4;
  const b = Buffer.alloc(40 + colorSize + maskStride * size);
  b.writeUInt32LE(40, 0); b.writeInt32LE(size, 4); b.writeInt32LE(size * 2, 8);
  b.writeUInt16LE(1, 12); b.writeUInt16LE(32, 14); b.writeUInt32LE(colorSize, 20);
  for (let y=0; y<size; ++y) for (let x=0; x<size; ++x) {
    const i=(y*size+x)*4, j=40+((size-1-y)*size+x)*4;
    b[j]=pixels[i+2]; b[j+1]=pixels[i+1]; b[j+2]=pixels[i]; b[j+3]=pixels[i+3];
    if(pixels[i+3]<128) b[40+colorSize+(size-1-y)*maskStride+(x>>3)] |= 0x80>>(x&7);
  }
  return b;
}
(async () => {
  const sizes = [16,20,24,32,48,64];
  const frames = await Promise.all(sizes.map(async size => dib(await rgba(size), size)));
  const header = Buffer.alloc(6 + sizes.length * 16); header.writeUInt16LE(1,2); header.writeUInt16LE(sizes.length,4);
  let offset=header.length;
  frames.forEach((frame,i) => { const p=6+i*16; header[p]=header[p+1]=sizes[i]; header.writeUInt16LE(1,p+4); header.writeUInt16LE(32,p+6); header.writeUInt32LE(frame.length,p+8); header.writeUInt32LE(offset,p+12); offset+=frame.length; });
  fs.writeFileSync(folder+'/mermaid.ico', Buffer.concat([header,...frames]));
  // Classic Notepad++ toolbar uses a 16x16 bitmap with a silver transparency key.
  const pixels = await sharp(await rgba(16), {raw:{width:16,height:16,channels:4}}).flatten({background:'#c0c0c0'}).raw().toBuffer();
  const stride=48, bmp=Buffer.alloc(54+stride*16); bmp.write('BM'); bmp.writeUInt32LE(bmp.length,2); bmp.writeUInt32LE(54,10);
  bmp.writeUInt32LE(40,14); bmp.writeInt32LE(16,18); bmp.writeInt32LE(16,22); bmp.writeUInt16LE(1,26); bmp.writeUInt16LE(24,28); bmp.writeUInt32LE(stride*16,34);
  for(let y=0;y<16;y++) for(let x=0;x<16;x++) {const i=(y*16+x)*3,j=54+(15-y)*stride+x*3; bmp[j]=pixels[i+2]; bmp[j+1]=pixels[i+1]; bmp[j+2]=pixels[i];}
  fs.writeFileSync(folder+'/mermaid.bmp',bmp);
  await sharp(source,{density:192}).resize(64,64).png().toFile(folder+'/mermaid-preview.png');
  console.log('Created Markdown + Mermaid ICO (16/20/24/32/48/64 px) and classic toolbar BMP.');
})().catch(error => { console.error(error); process.exitCode = 1; });
