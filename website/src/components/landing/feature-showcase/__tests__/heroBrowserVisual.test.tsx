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
  test("renders only the product capture poster during SSR", () => {
    // Given: the homepage hero is rendered before client policy resolution.
    const html = renderToStaticMarkup(<HeroBrowserVisual />);

    // Then: the native-size poster is the only initial media asset.
    expect(html).toContain('src="/demo/hero/maho-hero-workflow-poster.png"');
    expect(html).toContain('width="1040"');
    expect(html).toContain('height="720"');
    expect(html).not.toContain(".mp4");
    expect(html).not.toContain("<source");
  });

  test("pins the single product capture media pair in source", () => {
    // Given: the component source that defines the runtime media request contract.
    const componentSource = readFileSync(new URL("../HeroBrowserVisual.tsx", import.meta.url), "utf8");

    // Then: only the cookie-free workflow and poster paths are pinned.
    expect(componentSource).toContain('"/demo/hero/maho-hero-workflow.mp4"');
    expect(componentSource).toContain('"/demo/hero/maho-hero-workflow-poster.png"');
    expect(componentSource).toContain("aspect-[4/5]");
    expect(componentSource).toContain("sm:aspect-[1040/720]");
    expect(componentSource).toContain("object-[32%_center]");
    expect(componentSource).toContain("sm:object-center");
    expect(componentSource).not.toContain("/demo/hero/dom-hyperframes/");
  });

  test("exposes one labelled image without scene selector controls", () => {
    // Given: the server-rendered hero contract.
    const html = renderToStaticMarkup(<HeroBrowserVisual />);

    // Then: one accessible image wraps decorative media without the retired scene buttons.
    expect(html.match(/role="img"/g)).toHaveLength(1);
    expect(html).toContain('aria-label="Maho Browser product demo"');
    expect(html).toContain('aria-hidden="true"');
    expect(html).toContain('data-motion-surface="hero"');
    expect(html).not.toContain(">Spaces</button>");
    expect(html).not.toContain(">Split View</button>");
    expect(html).not.toContain(">Command Bar + AI</button>");
  });
});
