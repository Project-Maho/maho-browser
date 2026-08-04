export {};

const browserShellModuleUrl = "/shared/browser-shell.js";
const commandAiModuleUrl = "/shared/command-ai.js";
const { mountBrowserShell } = await import(browserShellModuleUrl);
const { createCommandAiScene } = await import(commandAiModuleUrl);

const cameraWrapper = document.querySelector("#command-ai-camera-wrapper");
const browserMount = document.querySelector("#command-ai-browser-mount");
const timelines = Reflect.get(window, "__timelines");
const timeline = timelines?.["command-ai"];

if (!(cameraWrapper instanceof HTMLElement) || !(browserMount instanceof HTMLElement)) {
  throw new Error("Command AI composition wrappers are missing");
}
if (!timeline) throw new Error("Command AI timeline is missing");
cameraWrapper.dataset.layoutAllowOverflow = "true";
cameraWrapper.dataset.layoutAllowOverlap = "true";
cameraWrapper.dataset.layoutAllowOcclusion = "true";

const browserShell = mountBrowserShell(browserMount);
if (!(browserShell instanceof HTMLElement)) throw new Error("Browser shell mount failed");
const contentSlot = browserShell.querySelector('[data-shell="browser-content"]');
if (!(contentSlot instanceof HTMLElement)) throw new Error("Browser content slot is missing");

const composition = createCommandAiScene({ state: "closed" });
composition.id = "command-ai-composition";
composition.dataset.continuousSplitContext = "true";
composition.dataset.layoutAllowOverflow = "true";
composition.dataset.layoutAllowOverlap = "true";
composition.dataset.layoutAllowOcclusion = "true";
composition.querySelector(".command-ai__overlay")?.remove();

const commandWrapper = createCommandAiScene({ state: "palette" });
commandWrapper.id = "command-ai-command-wrapper";
commandWrapper.classList.add("command-ai-state-wrapper");
commandWrapper.dataset.sceneWrapper = "command";
commandWrapper.dataset.sharedSplitContext = "command-ai-composition";
commandWrapper.querySelector(".command-ai__context")?.remove();

const aiWrapper = createCommandAiScene({ state: "ai" });
aiWrapper.id = "command-ai-ai-wrapper";
aiWrapper.classList.add("command-ai-state-wrapper");
aiWrapper.dataset.sceneWrapper = "ai";
aiWrapper.dataset.sharedSplitContext = "command-ai-composition";
aiWrapper.querySelector(".command-ai__context")?.remove();
composition.append(commandWrapper, aiWrapper);
contentSlot.append(composition);

timeline
  .set(cameraWrapper, { x: 0, y: 0, scale: 1, transformOrigin: "50% 50%" }, 0)
  .set([commandWrapper, aiWrapper], { autoAlpha: 0 }, 0)
  .to(commandWrapper, { autoAlpha: 1, duration: 0.4, ease: "power2.out" }, 4.6)
  .to(cameraWrapper, { x: 0, y: 0, scale: 1.09, duration: 1.5, ease: "power3.inOut" }, 5)
  .to(commandWrapper, { autoAlpha: 0, duration: 0.5, ease: "power2.in" }, 14.8)
  .to(aiWrapper, { autoAlpha: 1, duration: 0.6, ease: "power2.out" }, 15.4)
  .to(cameraWrapper, { x: -112, y: 0, scale: 1.09, duration: 2.4, ease: "power3.inOut" }, 16.4)
  .to(cameraWrapper, { x: -20, y: 0, scale: 1.02, duration: 2.4, ease: "power3.inOut" }, 24.2);
