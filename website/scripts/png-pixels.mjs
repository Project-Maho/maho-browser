import { inflateSync } from "node:zlib";

const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
const MAX_DIMENSION = 16_384;
const MAX_PIXEL_COUNT = 64 * 1024 * 1024;

function paethPredictor(left, up, upLeft) {
  const predictor = left + up - upLeft;
  const leftDistance = Math.abs(predictor - left);
  const upDistance = Math.abs(predictor - up);
  const upLeftDistance = Math.abs(predictor - upLeft);
  if (leftDistance <= upDistance && leftDistance <= upLeftDistance) return left;
  if (upDistance <= upLeftDistance) return up;
  return upLeft;
}

function parsePng(buffer) {
  if (!Buffer.isBuffer(buffer) && !(buffer instanceof Uint8Array)) {
    throw new TypeError("PNG input must be a byte buffer");
  }

  const png = Buffer.from(buffer);
  if (png.length < PNG_SIGNATURE.length || !png.subarray(0, 8).equals(PNG_SIGNATURE)) {
    throw new Error("Invalid PNG signature");
  }

  let offset = PNG_SIGNATURE.length;
  let ihdr = null;
  let sawIdat = false;
  let sawIend = false;
  const idatChunks = [];

  while (offset < png.length) {
    if (png.length - offset < 12) {
      throw new Error("Truncated PNG chunk header");
    }

    const length = png.readUInt32BE(offset);
    const payloadStart = offset + 8;
    const payloadEnd = payloadStart + length;
    const chunkEnd = payloadEnd + 4;
    if (payloadEnd > png.length || chunkEnd > png.length) {
      throw new Error("PNG chunk payload exceeds buffer bounds");
    }

    const type = png.toString("ascii", offset + 4, offset + 8);
    const payload = png.subarray(payloadStart, payloadEnd);
    if (type === "IHDR") {
      if (ihdr !== null || sawIdat) {
        throw new Error("PNG must contain exactly one IHDR before IDAT");
      }
      if (payload.length !== 13) {
        throw new Error("PNG IHDR must contain exactly 13 bytes");
      }
      ihdr = payload;
    } else if (type === "IDAT") {
      if (ihdr === null) {
        throw new Error("PNG IDAT must follow IHDR");
      }
      idatChunks.push(payload);
      sawIdat = true;
    } else if (type === "IEND") {
      if (payload.length !== 0 || chunkEnd !== png.length) {
        throw new Error("PNG IEND must be an empty final chunk");
      }
      sawIend = true;
      break;
    }

    offset = chunkEnd;
  }

  if (ihdr === null) {
    throw new Error("PNG is missing IHDR");
  }
  if (!sawIdat) {
    throw new Error("PNG is missing IDAT");
  }
  if (!sawIend) {
    throw new Error("PNG is missing IEND");
  }

  const width = ihdr.readUInt32BE(0);
  const height = ihdr.readUInt32BE(4);
  const bitDepth = ihdr[8];
  const colorType = ihdr[9];
  const compressionMethod = ihdr[10];
  const filterMethod = ihdr[11];
  const interlaceMethod = ihdr[12];

  if (
    width === 0 ||
    height === 0 ||
    width > MAX_DIMENSION ||
    height > MAX_DIMENSION ||
    width * height > MAX_PIXEL_COUNT
  ) {
    throw new Error("PNG dimensions are outside the supported bounds");
  }
  if (bitDepth !== 8) {
    throw new Error("PNG bit depth must be 8");
  }
  if (colorType !== 2 && colorType !== 6) {
    throw new Error("PNG color type must be RGB (2) or RGBA (6)");
  }
  if (compressionMethod !== 0) {
    throw new Error("PNG compression method must be 0");
  }
  if (filterMethod !== 0) {
    throw new Error("PNG filter method must be 0");
  }
  if (interlaceMethod !== 0) {
    throw new Error("Interlaced PNGs are not supported");
  }

  const sourceBytesPerPixel = colorType === 2 ? 3 : 4;
  const sourceStride = width * sourceBytesPerPixel;
  const expectedInflatedLength = height * (sourceStride + 1);
  const compressed = Buffer.concat(idatChunks);
  if (compressed.length === 0) {
    throw new Error("PNG IDAT payload is empty");
  }

  const scanlines = inflateSync(compressed, { maxOutputLength: expectedInflatedLength });
  if (scanlines.length !== expectedInflatedLength) {
    throw new Error("PNG inflated scanline length is invalid");
  }

  return {
    width,
    height,
    colorType,
    sourceBytesPerPixel,
    sourceStride,
    scanlines,
  };
}

export function decodePngPixels(buffer) {
  const { width, height, sourceBytesPerPixel, sourceStride, scanlines } = parsePng(buffer);
  const pixels = Buffer.alloc(width * height * 4);
  let scanlineOffset = 0;
  let outputOffset = 0;
  let previousRow = Buffer.alloc(sourceStride);
  let currentRow = Buffer.alloc(sourceStride);

  for (let rowIndex = 0; rowIndex < height; rowIndex++) {
    const filterType = scanlines[scanlineOffset++];
    if (filterType > 4) {
      throw new Error(`Unsupported PNG filter type: ${filterType}`);
    }

    for (let byteIndex = 0; byteIndex < sourceStride; byteIndex++) {
      const filteredByte = scanlines[scanlineOffset++];
      const left = byteIndex >= sourceBytesPerPixel ? currentRow[byteIndex - sourceBytesPerPixel] : 0;
      const up = rowIndex > 0 ? previousRow[byteIndex] : 0;
      const upLeft = rowIndex > 0 && byteIndex >= sourceBytesPerPixel
        ? previousRow[byteIndex - sourceBytesPerPixel]
        : 0;

      let reconstructedByte = filteredByte;
      if (filterType === 1) {
        reconstructedByte = (filteredByte + left) & 0xff;
      } else if (filterType === 2) {
        reconstructedByte = (filteredByte + up) & 0xff;
      } else if (filterType === 3) {
        reconstructedByte = (filteredByte + Math.floor((left + up) / 2)) & 0xff;
      } else if (filterType === 4) {
        reconstructedByte = (filteredByte + paethPredictor(left, up, upLeft)) & 0xff;
      }
      currentRow[byteIndex] = reconstructedByte;
    }

    for (let pixelIndex = 0; pixelIndex < width; pixelIndex++) {
      const sourceOffset = pixelIndex * sourceBytesPerPixel;
      pixels[outputOffset++] = currentRow[sourceOffset];
      pixels[outputOffset++] = currentRow[sourceOffset + 1];
      pixels[outputOffset++] = currentRow[sourceOffset + 2];
      pixels[outputOffset++] = sourceBytesPerPixel === 4 ? currentRow[sourceOffset + 3] : 255;
    }

    const completedRow = currentRow;
    currentRow = previousRow;
    previousRow = completedRow;
  }

  return { width, height, pixels };
}
