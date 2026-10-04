import i18n from "i18next";
import { initReactI18next } from "react-i18next";
import en from "./locales/en.json";
import ko from "./locales/ko.json";
import { loadMailBehaviorPrefs } from "../hooks/useSettings";

i18n.use(initReactI18next).init({
  resources: {
    en: { translation: en },
    ko: { translation: ko },
  },
  lng: "en",
  fallbackLng: "en",
  interpolation: { escapeValue: false },
});

const syncLanguage = () => {
  void loadMailBehaviorPrefs().then(
    ({ language }) => i18n.changeLanguage(language),
    (error: unknown) => {
      console.error("Failed to load mail language preference", error);
    },
  );
};

syncLanguage();
window.addEventListener("focus", syncLanguage);

export default i18n;
