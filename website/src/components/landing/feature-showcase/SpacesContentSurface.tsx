import React from "react";
import { Search } from "lucide-react";
import type { WorkspaceDefinition } from "./data/workspaces";

export interface SpacesContentSurfaceProps {
  readonly workspace: WorkspaceDefinition;
  readonly opacity: number;
  readonly hidden: boolean;
}

function FilesSurface(): React.ReactNode {
  return (
    <div className="h-full w-full bg-black pl-[18%]">
      <div className="grid h-full place-items-center overflow-hidden">
        <div className="h-[118%] w-[42%] rotate-[0.2deg] rounded-[0.35cqw] bg-[url('/demo/monet-water-lilies-1906.jpg')] bg-cover bg-center shadow-[0_0_0_1px_rgba(255,255,255,0.08),0_2cqw_7cqw_rgba(0,0,0,0.5)]" />
      </div>
    </div>
  );
}

function GoogleSurface(): React.ReactNode {
  return (
    <div className="grid h-full place-items-center bg-[#202124] text-[#e8eaed]">
      <div className="w-[58%] text-center">
        <div className="text-[4.2cqw] font-semibold tracking-[-0.08em]">
          <span className="text-[#4285f4]">G</span><span className="text-[#ea4335]">o</span><span className="text-[#fbbc05]">o</span><span className="text-[#4285f4]">g</span><span className="text-[#34a853]">l</span><span className="text-[#ea4335]">e</span>
        </div>
        <div className="mt-[1.4cqw] flex h-[3.2cqw] items-center gap-[0.8cqw] rounded-full border border-[#5f6368] bg-[#303134] px-[1.2cqw] text-left text-[1cqw] text-[#bdc1c6]">
          <Search className="h-[1.1cqw] w-[1.1cqw]" />
          <span>Search Google or type a URL</span>
        </div>
        <div className="mt-[1.3cqw] flex justify-center gap-[0.7cqw] text-[0.75cqw] text-[#bdc1c6]">
          {['Google Search', "I'm Feeling Lucky"].map((label) => <span key={label} className="rounded-[0.35cqw] bg-[#303134] px-[0.9cqw] py-[0.45cqw]">{label}</span>)}
        </div>
      </div>
    </div>
  );
}

function WolframSurface(): React.ReactNode {
  return (
    <div className="h-full w-full bg-[#262626] px-[4.5cqw] py-[3.6cqw] text-[#e5e5e5]">
      <div className="flex items-center justify-between text-[0.8cqw] text-zinc-400">
        <span>WOLFRAM<span className="text-[#f28c28]">|</span>ALPHA</span>
        <span>Examples · Random · Pro</span>
      </div>
      <div className="mt-[7cqw] text-center">
        <h1 className="text-[3.6cqw] font-light tracking-[-0.06em]"><span className="font-semibold">Wolfram</span><span className="text-[#f28c28]">Alpha</span></h1>
        <div className="mx-auto mt-[1.3cqw] flex h-[3.4cqw] max-w-[44cqw] items-center rounded-[0.25cqw] bg-[#f28c28] p-[0.28cqw] shadow-[0_1cqw_3cqw_rgba(0,0,0,0.35)]">
          <div className="flex-1 rounded-[0.15cqw] bg-white px-[1cqw] py-[0.72cqw] text-left text-[1cqw] text-zinc-500">enter what you want to calculate or know about</div>
          <div className="grid h-full w-[3.2cqw] place-items-center text-[1.25cqw] text-white">=</div>
        </div>
        <div className="mx-auto mt-[2.4cqw] grid max-w-[47cqw] grid-cols-4 gap-[0.8cqw] text-left text-[0.72cqw] text-zinc-300">
          {['Mathematics', 'Science', 'Technology', 'Society', 'Culture', 'Everyday Life', 'Units', 'Data'].map((label) => <div key={label} className="rounded-[0.45cqw] border border-zinc-700 bg-zinc-800/70 px-[0.8cqw] py-[0.7cqw]">{label}</div>)}
        </div>
      </div>
    </div>
  );
}

function AeonSurface(): React.ReactNode {
  return (
    <div className="h-full overflow-hidden bg-white text-zinc-950">
      <header className="border-b border-zinc-200 px-[3cqw] py-[1.2cqw]">
        <div className="flex items-center justify-between text-[0.7cqw] font-semibold uppercase tracking-[0.16em] text-zinc-500">
          <span>Menu / Search</span>
          <span>Donate / Newsletter / Sign in</span>
        </div>
        <div className="text-center font-serif text-[3.5cqw] font-semibold tracking-[-0.08em] text-[#7d1f2f]">aeon</div>
      </header>
      <section className="relative h-[26cqw] overflow-hidden bg-[#741f28] text-white">
        <div className="absolute inset-y-0 left-0 w-[30%] bg-[#f0efe9]" />
        <div className="absolute inset-y-0 right-0 w-[70%] bg-[radial-gradient(circle_at_30%_42%,rgba(255,255,255,0.25),transparent_19%),linear-gradient(135deg,#a23a42,#5d1119)]" />
        <div className="relative ml-auto grid h-full w-[70%] place-items-center text-center">
          <div>
            <h1 className="font-serif text-[3cqw] leading-none tracking-[-0.055em]">Signal then storm</h1>
            <p className="mx-auto mt-[0.9cqw] max-w-[32cqw] text-[0.9cqw] leading-relaxed text-white/88">Allan Basbaum has spent 50 years searching the nervous system for the places where pain begins.</p>
          </div>
        </div>
      </section>
      <section className="grid grid-cols-3 gap-[1.2cqw] px-[2.5cqw] py-[1.4cqw]">
        {['The savage bites back', 'Rooms that keep changing', "The people's atheist"].map((title) => <article key={title}><div className="h-[7cqw] rounded-[0.4cqw] bg-gradient-to-br from-stone-200 via-rose-100 to-zinc-400" /><h3 className="mt-[0.55cqw] font-serif text-[1.15cqw] leading-tight">{title}</h3></article>)}
      </section>
    </div>
  );
}

function Surface({ workspace }: { readonly workspace: WorkspaceDefinition }): React.ReactNode {
  switch (workspace.key) {
    case "files": return <FilesSurface />;
    case "media": return <GoogleSurface />;
    case "research": return <WolframSurface />;
    case "reading": return <AeonSurface />;
  }
}

export const SpacesContentSurface: React.FC<SpacesContentSurfaceProps> = ({ workspace, opacity, hidden }) => (
  <div
    data-showcase="content-layer"
    data-content-index={workspace.index}
    aria-hidden={hidden}
    className="absolute inset-0 block transition-opacity duration-150"
    style={{ opacity }}
  >
    <Surface workspace={workspace} />
  </div>
);
