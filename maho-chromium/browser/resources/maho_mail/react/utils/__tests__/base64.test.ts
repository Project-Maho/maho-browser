import { describe, it, expect } from "vitest";
import { base64ToUint8Array, uint8ArrayToBase64 } from "../base64";

describe("base64ToUint8Array", () => {
  it("decodes a simple ASCII string", () => {
    const encoded = btoa("Hello, attachment!");
    const result = base64ToUint8Array(encoded);
    expect(result).toEqual(new Uint8Array([72, 101, 108, 108, 111, 44, 32, 97, 116, 116, 97, 99, 104, 109, 101, 110, 116, 33]));
  });

  it("handles binary data with high bytes", () => {
    const bytes = new Uint8Array([0x00, 0x01, 0xFF, 0xFE, 0x80]);
    const b64 = uint8ArrayToBase64(bytes);
    const result = base64ToUint8Array(b64);
    expect(result).toEqual(bytes);
  });

  it("returns empty array for empty string", () => {
    expect(base64ToUint8Array("")).toEqual(new Uint8Array([]));
  });
});

describe("uint8ArrayToBase64", () => {
  it("encodes a simple byte array", () => {
    const bytes = new Uint8Array([72, 101, 108, 108, 111]);
    const result = uint8ArrayToBase64(bytes);
    expect(atob(result)).toBe("Hello");
  });

  it("roundtrips arbitrary binary data", () => {
    const original = new Uint8Array(256);
    for (let i = 0; i < 256; i++) original[i] = i;
    const encoded = uint8ArrayToBase64(original);
    const decoded = base64ToUint8Array(encoded);
    expect(decoded).toEqual(original);
  });

  it("returns empty string for empty array", () => {
    expect(uint8ArrayToBase64(new Uint8Array([]))).toBe("");
  });
});
