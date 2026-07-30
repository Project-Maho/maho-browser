import { useState, useEffect } from "react";
import { X, ChevronRight, ChevronLeft, Inbox, Search, PenLine, Settings, Sparkles, BrainCircuit } from "lucide-react";
import { Dialog, DialogContent, DialogTitle, DialogDescription } from "../ui/dialog";

interface TourStep {
  title: string;
  description: string;
  icon: React.ReactNode;
}

const TOUR_STEPS: TourStep[] = [
  {
    title: "Welcome to Maho Mail",
    description: "A modern, AI-powered email client built for speed and privacy. Let's take a quick tour of the key features.",
    icon: <Inbox size={24} className="text-primary" />,
  },
  {
    title: "Smart Inbox",
    description: "AI automatically categorizes your emails into Personal, Notifications, Newsletters, and Promotions for a cleaner inbox.",
    icon: <BrainCircuit size={24} className="text-violet-400" />,
  },
  {
    title: "AI Assistant",
    description: "Summarize emails, draft replies, classify messages, and translate content — all powered by your choice of AI provider.",
    icon: <Sparkles size={24} className="text-amber-400" />,
  },
  {
    title: "Powerful Search",
    description: "Find any email instantly with keyword search, or use natural language by prefixing your query with '?' for AI-powered search.",
    icon: <Search size={24} className="text-emerald-400" />,
  },
  {
    title: "Compose & Schedule",
    description: "Write emails with templates and signatures. Schedule sends for later, or use undo send for a 5-second safety net.",
    icon: <PenLine size={24} className="text-cyan-400" />,
  },
  {
    title: "Customize Everything",
    description: "Set up your AI provider, manage signatures and templates, create labels, and adjust the look and feel in Settings.",
    icon: <Settings size={24} className="text-muted-foreground" />,
  },
];

const STORAGE_KEY = "maho-onboarding-completed";

interface OnboardingTourProps {
  forceShow?: boolean;
  onComplete?: () => void;
}

export function OnboardingTour({ forceShow, onComplete }: OnboardingTourProps) {
  const [step, setStep] = useState(0);
  const [visible, setVisible] = useState(false);

  useEffect(() => {
    localStorage.setItem(STORAGE_KEY, "true");
    if (forceShow) {
      setVisible(true);
      setStep(0);
      return;
    }
    const completed = localStorage.getItem(STORAGE_KEY);
    if (!completed) {
      setVisible(true);
    }
  }, [forceShow]);

  function complete() {
    localStorage.setItem(STORAGE_KEY, "true");
    setVisible(false);
    onComplete?.();
  }

  function next() {
    if (step < TOUR_STEPS.length - 1) {
      setStep(step + 1);
    } else {
      complete();
    }
  }

  function prev() {
    if (step > 0) setStep(step - 1);
  }

  const currentStep = TOUR_STEPS[step];
  const isLast = step === TOUR_STEPS.length - 1;

  return (
    <Dialog open={visible} onOpenChange={(open) => !open && complete()}>
      <DialogContent hideDefaultClose className="mx-4 w-full max-w-md rounded-2xl border border-border bg-background p-6 shadow-2xl sm:rounded-2xl">
        <DialogTitle className="sr-only">{currentStep.title}</DialogTitle>
        <DialogDescription className="sr-only">Onboarding tour step {step + 1} of {TOUR_STEPS.length}</DialogDescription>
        <div className="mb-6 flex items-start justify-between">
          <div className="flex h-12 w-12 items-center justify-center rounded-xl bg-card">
            {currentStep.icon}
          </div>
          <button
            type="button"
            onClick={complete}
            className="rounded-lg p-1 text-muted-foreground transition-colors hover:bg-card hover:text-muted-foreground"
            aria-label="Skip tour"
            title="Skip tour"
          >
            <X size={18} />
          </button>
        </div>

        <h3 className="mb-2 text-lg font-semibold text-foreground">{currentStep.title}</h3>
        <p className="mb-8 text-sm leading-relaxed text-muted-foreground">{currentStep.description}</p>

        <div className="flex items-center justify-between">
          <div className="flex gap-1.5">
            {TOUR_STEPS.map((_, i) => (
              <div
                key={i}
                className={`h-1.5 rounded-full transition-all ${
                  i === step ? "w-6 bg-primary" : "w-1.5 bg-muted"
                }`}
              />
            ))}
          </div>

          <div className="flex items-center gap-2">
            {step > 0 && (
              <button
                type="button"
                onClick={prev}
                className="flex items-center gap-1 rounded-lg px-3 py-2 text-sm text-muted-foreground transition-colors hover:bg-card hover:text-foreground"
              >
                <ChevronLeft size={14} /> Back
              </button>
            )}
            <button
              type="button"
              onClick={next}
              className="flex items-center gap-1 rounded-lg bg-primary px-4 py-2 text-sm font-medium text-white transition-colors hover:bg-primary/80"
            >
              {isLast ? "Get Started" : "Next"} {!isLast && <ChevronRight size={14} />}
            </button>
          </div>
        </div>
      </DialogContent>
    </Dialog>
  );
}
