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
  test("renders only the Spaces poster during SSR", () => {
    // Given: the homepage hero is rendered before the browser evaluates motion policy.
    const html = renderToStaticMarkup(<HeroBrowserVisual />);

    // Then: Spaces is the initial poster and no video network URL is emitted.
    expect(html).toContain('src="/demo/hero/dom-hyperframes/maho-spaces-dom-poster.webp"');
    expect(html).toContain('width="1920"');
    expect(html).toContain('height="1080"');
    expect(html).not.toContain(".mp4");
    expect(html).not.toContain("<source");
  });

  test("pins only final independent scene media in source", () => {
    // Given: the component source that defines the runtime media request contract.
    const componentSource = readFileSync(new URL("../HeroBrowserVisual.tsx", import.meta.url), "utf8");

    // Then: each selected scene uses a final DOM-rendered asset, never an aggregate master.
    expect(componentSource).toContain('"/demo/hero/dom-hyperframes/maho-spaces-dom.mp4"');
    expect(componentSource).toContain('"/demo/hero/dom-hyperframes/maho-split-view-dom.mp4"');
    expect(componentSource).toContain('"/demo/hero/dom-hyperframes/maho-command-ai-dom.mp4"');
    expect(componentSource).not.toContain("/demo/hero/hyperframes/");
    expect(componentSource).not.toContain("master");
  });

  test("exposes one labelled image and scene selector controls", () => {
    // Given: the server-rendered hero contract.
    const html = renderToStaticMarkup(<HeroBrowserVisual />);

    // Then: the hero describes its selected scene and exposes its selectable alternatives.
    expect(html.match(/role="img"/g)).toHaveLength(1);
    expect(html).toContain('aria-label="Maho Browser hero showing Spaces"');
    expect(html).toContain('aria-hidden="true"');
    expect(html).toContain('data-motion-surface="hero"');
    expect(html).toContain(">Spaces</button>");
    expect(html).toContain(">Split View</button>");
    expect(html).toContain(">Command Bar + AI</button>");
  });
});
