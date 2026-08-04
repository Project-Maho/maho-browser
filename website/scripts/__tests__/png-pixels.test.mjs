import assert from "node:assert/strict";
import { deflateSync } from "node:zlib";
import { describe, test } from "bun:test";
import { decodePngPixels } from "../png-pixels.mjs";

const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

function chunk(type, payload) {
  const result = Buffer.alloc(12 + payload.length);
  result.writeUInt32BE(payload.length, 0);
  result.write(type, 4, "ascii");
  payload.copy(result, 8);
  return result;
}

function makePng({ width, height, colorType, rows, ihdr = {}, chunks = {} }) {
  const ihdrPayload = Buffer.alloc(13);
  ihdrPayload.writeUInt32BE(width, 0);
  ihdrPayload.writeUInt32BE(height, 4);
  ihdrPayload[8] = ihdr.bitDepth ?? 8;
  ihdrPayload[9] = colorType;
  ihdrPayload[10] = ihdr.compressionMethod ?? 0;
  ihdrPayload[11] = ihdr.filterMethod ?? 0;
  ihdrPayload[12] = ihdr.interlaceMethod ?? 0;

  const scanlines = Buffer.concat(rows.map((row) => Buffer.from(row)));
  const idat = chunks.idat ?? deflateSync(scanlines);
  const encodedChunks = chunks.encoded ?? [
    chunk("IHDR", ihdrPayload),
    chunk("IDAT", idat),
    chunk("IEND", Buffer.alloc(0)),
  ];
  return Buffer.concat([PNG_SIGNATURE, ...encodedChunks]);
}

const RGB_ROWS = [
  [10, 20, 30, 40, 50, 60],
  [100, 110, 120, 130, 140, 150],
];
const RGBA_ROWS = [
  [10, 20, 30, 40, 50, 60, 70, 80],
  [100, 110, 120, 130, 140, 150, 160, 170],
];

const FILTERED_ROWS = {
  0: {
    RGB: [
      [0, 10, 20, 30, 40, 50, 60],
      [0, 100, 110, 120, 130, 140, 150],
    ],
    RGBA: [
      [0, 10, 20, 30, 40, 50, 60, 70, 80],
      [0, 100, 110, 120, 130, 140, 150, 160, 170],
    ],
  },
  1: {
    RGB: [
      [1, 10, 20, 30, 30, 30, 30],
      [1, 100, 110, 120, 30, 30, 30],
    ],
    RGBA: [
      [1, 10, 20, 30, 40, 40, 40, 40, 40],
      [1, 100, 110, 120, 130, 40, 40, 40, 40],
    ],
  },
  2: {
    RGB: [
      [2, 10, 20, 30, 40, 50, 60],
      [2, 90, 90, 90, 90, 90, 90],
    ],
    RGBA: [
      [2, 10, 20, 30, 40, 50, 60, 70, 80],
      [2, 90, 90, 90, 90, 90, 90, 90, 90],
    ],
  },
  3: {
    RGB: [
      [3, 10, 20, 30, 35, 40, 45],
      [3, 95, 100, 105, 60, 60, 60],
    ],
    RGBA: [
      [3, 10, 20, 30, 40, 45, 50, 55, 60],
      [3, 95, 100, 105, 110, 65, 65, 65, 65],
    ],
  },
  4: {
    RGB: [
      [4, 10, 20, 30, 30, 30, 30],
      [4, 90, 90, 90, 30, 30, 30],
    ],
    RGBA: [
      [4, 10, 20, 30, 40, 40, 40, 40, 40],
      [4, 90, 90, 90, 90, 40, 40, 40, 40],
    ],
  },
};

function expectedRgba(rows, colorType) {
  const result = [];
  const bytesPerPixel = colorType === 2 ? 3 : 4;
  for (const row of rows) {
    for (let offset = 0; offset < row.length; offset += bytesPerPixel) {
      result.push(row[offset], row[offset + 1], row[offset + 2]);
      result.push(colorType === 2 ? 255 : row[offset + 3]);
    }
  }
  return result;
}

describe("decodePngPixels scanline reconstruction", () => {
  for (const [filterType, colorRows] of Object.entries(FILTERED_ROWS)) {
    for (const [name, colorType] of [["RGB", 2], ["RGBA", 6]]) {
      test(`reconstructs filter ${filterType} for ${name}`, () => {
        const sourceRows = colorType === 2 ? RGB_ROWS : RGBA_ROWS;
        const png = makePng({
          width: 2,
          height: 2,
          colorType,
          rows: colorRows[name],
        });

        const decoded = decodePngPixels(png);

        assert.equal(decoded.width, 2);
        assert.equal(decoded.height, 2);
        assert.deepEqual([...decoded.pixels], expectedRgba(sourceRows, colorType));
      });
    }
  }

  test("uses the previous row for Paeth up-left instead of the current row", () => {
    const png = makePng({
      width: 2,
      height: 2,
      colorType: 2,
      rows: [
        [0, 100, 0, 0, 200, 0, 0],
        [4, 166, 0, 0, 50, 20, 30],
      ],
    });

    const decoded = decodePngPixels(png);

    assert.deepEqual([...decoded.pixels], [
      100, 0, 0, 255, 200, 0, 0, 255,
      10, 0, 0, 255, 150, 20, 30, 255,
    ]);
  });
});

describe("decodePngPixels PNG validation", () => {
  const validRows = [[0, 10, 20, 30]];
  const validPng = () => makePng({ width: 1, height: 1, colorType: 2, rows: validRows });

  test("rejects an invalid PNG signature", () => {
    const png = validPng();
    png[0] = 0;
    assert.throws(() => decodePngPixels(png), /signature/i);
  });

  test("rejects truncated chunk headers and payloads", () => {
    const png = validPng();
    assert.throws(() => decodePngPixels(png.subarray(0, 20)), /chunk|truncated/i);

    const badPayloadLength = Buffer.from(png);
    badPayloadLength.writeUInt32BE(99, 8);
    assert.throws(() => decodePngPixels(badPayloadLength), /chunk|bounds|truncated/i);
  });

  test("requires exactly one IHDR before IDAT", () => {
    const png = validPng();
    const ihdr = png.subarray(8, 33);
    const idat = png.subarray(33, png.length - 12);
    const iend = png.subarray(png.length - 12);
    assert.throws(
      () => decodePngPixels(Buffer.concat([PNG_SIGNATURE, idat, ihdr, iend])),
      /IHDR.*before.*IDAT|IDAT.*IHDR/i,
    );
    assert.throws(
      () => decodePngPixels(Buffer.concat([PNG_SIGNATURE, ihdr, ihdr, idat, iend])),
      /IHDR/i,
    );
  });

  test("requires positive bounded dimensions", () => {
    assert.throws(
      () => decodePngPixels(makePng({ width: 0, height: 1, colorType: 2, rows: validRows })),
      /dimension/i,
    );
    assert.throws(
      () => decodePngPixels(makePng({ width: 16_385, height: 1, colorType: 2, rows: [] })),
      /dimension/i,
    );
  });

  test("accepts only 8-bit RGB and RGBA with supported IHDR methods", () => {
    for (const ihdr of [
      { bitDepth: 16 },
      { compressionMethod: 1 },
      { filterMethod: 1 },
      { interlaceMethod: 1 },
    ]) {
      assert.throws(
        () => decodePngPixels(makePng({ width: 1, height: 1, colorType: 2, rows: validRows, ihdr })),
        /bit depth|compression|filter method|interlac/i,
      );
    }
    assert.throws(
      () => decodePngPixels(makePng({ width: 1, height: 1, colorType: 0, rows: validRows })),
      /color type/i,
    );
  });

  test("requires IDAT and IEND chunks", () => {
    const png = validPng();
    const ihdr = png.subarray(8, 33);
    const iend = png.subarray(png.length - 12);
    assert.throws(() => decodePngPixels(Buffer.concat([PNG_SIGNATURE, ihdr, iend])), /IDAT/i);
    assert.throws(() => decodePngPixels(Buffer.concat([PNG_SIGNATURE, ihdr, png.subarray(33, png.length - 12)])), /IEND/i);
  });

  test("requires the exact inflated scanline length and a filter byte from 0 through 4", () => {
    const extraByte = makePng({ width: 1, height: 1, colorType: 2, rows: [[0, 10, 20, 30, 40]] });
    assert.throws(() => decodePngPixels(extraByte), /length|scanline|Buffer/i);

    const invalidFilter = makePng({ width: 1, height: 1, colorType: 2, rows: [[5, 10, 20, 30]] });
    assert.throws(() => decodePngPixels(invalidFilter), /filter/i);
  });
});
