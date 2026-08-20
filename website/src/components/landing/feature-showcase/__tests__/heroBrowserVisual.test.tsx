import { readFileSync } from "node:fs";
import { renderToStaticMarkup } from "react-dom/server";
import { HeroBrowserVisual } from "../HeroBrowserVisual";

type Matcher = {
  readonly toContain: (expected: string) => void;
  readonly toHaveLength: (expected: number) => void;
  readonly not: { readonly toContain: (expected: string) => void };
};

declare function describe(name: string, callback: () => void): void;
declare function test(name: string, callback: () => void): void;
declare function expect(value: unknown): Matcher;

describe("HeroBrowserVisual component", () => {
  test("renders the user-recorded Hero poster during SSR", () => {
    // Given: the homepage hero is rendered before the browser evaluates motion policy.
    const html = renderToStaticMarkup(<HeroBrowserVisual />);

    // Then: the original user-recorded capture is the static fallback with no video request.
    expect(html).toContain('src="/demo/hero/maho-hero-capture-poster.png"');
    expect(html).toContain('width="1920"');
    expect(html).toContain('height="1080"');
    expect(html).not.toContain(".mp4");
    expect(html).not.toContain("<source");
  });

  test("pins the original user-recorded capture as the only Hero media", () => {
    // Given: the component source that defines the runtime media request contract.
    const componentSource = readFileSync(new URL("../HeroBrowserVisual.tsx", import.meta.url), "utf8");

    // Then: synthetic Hero scene renders cannot replace the recorded product demo.
    expect(componentSource).toContain('"/demo/hero/maho-hero-capture.mp4"');
    expect(componentSource).toContain('"/demo/hero/maho-hero-capture-poster.png"');
    expect(componentSource).not.toContain("/demo/hero/dom-hyperframes/");
    expect(componentSource).not.toContain("/demo/hero/hyperframes/");
  });

  test("exposes one labelled recorded Hero image without synthetic scene controls", () => {
    // Given: the server-rendered hero contract.
    const html = renderToStaticMarkup(<HeroBrowserVisual />);

    // Then: the hero stays focused on the user's recorded product demo.
    expect(html.match(/role="img"/g)).toHaveLength(1);
    expect(html).toContain('aria-label="Maho Browser product recording"');
    expect(html).toContain('aria-hidden="true"');
    expect(html).toContain('data-motion-surface="hero"');
    expect(html).not.toContain(">Spaces</button>");
    expect(html).not.toContain(">Split View</button>");
    expect(html).not.toContain(">Command Bar + AI</button>");
  });
});
