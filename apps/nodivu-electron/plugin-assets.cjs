const fs = require('node:fs/promises');
const path = require('node:path');
const { PNG } = require('image-size/types/png');
const { JPG } = require('image-size/types/jpg');
const { WEBP } = require('image-size/types/webp');
const { GIF } = require('image-size/types/gif');

const imageFormats = new Map([
  ['.png', [PNG, 'image/png']], ['.jpg', [JPG, 'image/jpeg']],
  ['.jpeg', [JPG, 'image/jpeg']], ['.webp', [WEBP, 'image/webp']],
  ['.gif', [GIF, 'image/gif']]
]);

async function readBounded(file, limit, label) {
  const handle = await fs.open(file, 'r');
  try {
    const stat = await handle.stat();
    if (!stat.isFile() || stat.size > limit) throw new Error(label + ' excede o limite ou não é arquivo.');
    // Bound reads even when a package or display cache changes after stat().
    const data = Buffer.alloc(stat.size + 1);
    let length = 0;
    while (length < data.length) {
      const { bytesRead } = await handle.read(data, length, data.length - length);
      if (!bytesRead) break;
      length += bytesRead;
    }
    if (length !== stat.size) throw new Error(label + ' mudou durante a leitura.');
    return data.subarray(0, length);
  } finally {
    await handle.close();
  }
}

async function readImage(file, name) {
  const format = imageFormats.get(path.extname(name).toLowerCase());
  if (!format) throw new Error('Imagem inválida: formato não suportado.');
  const data = await readBounded(file, 2 * 1024 * 1024, 'Imagem');
  const [header, mime] = format;
  let dimensions;
  try {
    if (header.validate(data) && (header !== PNG || data[0] === 0x89)) dimensions = header.calculate(data);
  } catch {
    // A truncated or mismatched header must not reach the renderer.
  }
  if (!dimensions) throw new Error('Imagem inválida: cabeçalho incompatível com a extensão.');
  const { width, height } = dimensions;
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width < 1 || height < 1 ||
      width > 4096 || height > 4096 || width * height > 8 * 1024 * 1024)
    throw new Error('Imagem excede o limite de dimensões da loja.');
  return { url: 'data:' + mime + ';base64,' + data.toString('base64') };
}

module.exports = { readBounded, readImage };
