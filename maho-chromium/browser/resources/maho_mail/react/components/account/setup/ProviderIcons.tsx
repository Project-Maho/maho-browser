import type { SVGProps } from 'react';
import { Mail } from 'lucide-react';

export interface ProviderIconProps extends Omit<SVGProps<SVGSVGElement>, 'width' | 'height'> {
  className?: string;
  size?: number;
}

function getIconProps(className: string | undefined, size: number) {
  return {
    width: size,
    height: size,
    viewBox: '0 0 24 24',
    className,
    'aria-hidden': true,
    focusable: false,
  } as const;
}

export function GmailIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <rect x="3" y="5" width="18" height="14" rx="2" fill="#FFFFFF" />
      <path d="M5 19V8.25L12 13.5L19 8.25V19H16V11.4L12 14.4L8 11.4V19H5Z" fill="#34A853" />
      <path d="M5 8.25V19H3.75C3.34 19 3 18.66 3 18.25V7.75C3 7.39 3.2 7.07 3.52 6.9L5 8.25Z" fill="#4285F4" />
      <path d="M19 8.25V19H20.25C20.66 19 21 18.66 21 18.25V7.75C21 7.39 20.8 7.07 20.48 6.9L19 8.25Z" fill="#FBBC05" />
      <path d="M3.52 6.9C3.75 6.78 4 6.72 4.25 6.72H4.92L12 12.03L19.08 6.72H19.75C20 6.72 20.25 6.78 20.48 6.9L12 13.26L3.52 6.9Z" fill="#EA4335" />
      <path d="M4.25 5H19.75C20.04 5 20.31 5.08 20.55 5.21L12 11.63L3.45 5.21C3.69 5.08 3.96 5 4.25 5Z" fill="#EA4335" />
    </svg>
  );
}

export function OutlookIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <rect x="9" y="4" width="11" height="16" rx="2" fill="#0078D4" opacity="0.92" />
      <path d="M10 7H20V9.22L15 12.4L10 9.22V7Z" fill="#2B88D8" />
      <path d="M10 9.22L15 12.4L20 9.22V17C20 18.1 19.1 19 18 19H12C10.9 19 10 18.1 10 17V9.22Z" fill="#005A9E" />
      <path d="M10 9.22V17C10 18.1 10.9 19 12 19H18C19.1 19 20 18.1 20 17V9.22L15 12.4L10 9.22Z" fill="#0078D4" />
      <rect x="4" y="6" width="9.5" height="12" rx="1.6" fill="#106EBE" />
      <path d="M8.75 8.7C6.87 8.7 5.65 10.06 5.65 12.02C5.65 13.98 6.87 15.34 8.75 15.34C10.64 15.34 11.85 13.98 11.85 12.02C11.85 10.06 10.64 8.7 8.75 8.7ZM8.75 10.18C9.67 10.18 10.16 10.91 10.16 12.02C10.16 13.13 9.67 13.86 8.75 13.86C7.83 13.86 7.34 13.13 7.34 12.02C7.34 10.91 7.83 10.18 8.75 10.18Z" fill="#FFFFFF" />
    </svg>
  );
}

export function YahooIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <path d="M6.6 5H9.36L11.98 9.33L14.56 5H17.3L13.2 11.45V16.35H10.7V11.47L6.6 5Z" fill="#6001D2" />
      <path d="M14.8 13.55H17.15L16.3 19H14.3L14.8 13.55Z" fill="#6001D2" />
      <circle cx="15.82" cy="20.45" r="1.2" fill="#6001D2" />
    </svg>
  );
}

export function ICloudIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <path d="M8.2 18C5.88 18 4 16.18 4 13.95C4 11.97 5.49 10.31 7.46 9.97C8.15 7.65 10.3 6 12.88 6C15.88 6 18.35 8.23 18.72 11.14C20.05 11.47 21 12.64 21 14C21 16.21 19.16 18 16.88 18H8.2Z" fill="#007AFF" />
      <path d="M7.3 17C5.82 17 4.62 15.87 4.62 14.46C4.62 13.21 5.57 12.16 6.84 11.95C7.29 10.48 8.66 9.43 10.31 9.43C12.23 9.43 13.81 10.84 14.05 12.68C14.9 12.89 15.52 13.61 15.52 14.46C15.52 15.87 14.34 17 12.87 17H7.3Z" fill="#4DA3FF" opacity="0.72" />
    </svg>
  );
}

export function FastmailIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <rect x="4" y="6" width="16" height="12" rx="2" fill="#3D4FE0" opacity="0.16" />
      <path d="M5 8.5L12 13.25L19 8.5V10.4L12 15.15L5 10.4V8.5Z" fill="#3D4FE0" />
      <path d="M6.6 7H18.55L14.05 12.05H10.45L6.6 7Z" fill="#3D4FE0" />
      <path d="M12.8 12.05H16.75L12.1 17.25H8.4L12.8 12.05Z" fill="#3D4FE0" />
    </svg>
  );
}

export function ZohoIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <rect x="4" y="5" width="16" height="14" rx="2.5" fill="#FFFFFF" />
      <path d="M7 7.5H17V9L10.15 15H17V16.5H7V15L13.85 9H7V7.5Z" fill="#E42527" />
      <path d="M7 16.5L17 7.5" stroke="#34A853" strokeWidth="1.4" strokeLinecap="round" opacity="0.9" />
    </svg>
  );
}

export function AolIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <path d="M7.2 16.9H5L8.65 7H11.15L14.8 16.9H12.6L11.9 14.92H7.92L7.2 16.9ZM9.9 9.5L8.47 13.45H11.36L9.9 9.5Z" fill="#31459B" />
      <path d="M15.35 10.95C16.93 10.95 17.98 12.02 17.98 13.72V16.9H16.02V14.02C16.02 13.04 15.58 12.48 14.75 12.48C13.93 12.48 13.4 13.09 13.4 14.06V16.9H11.44V11.1H13.31V11.78C13.75 11.26 14.44 10.95 15.35 10.95Z" fill="#31459B" />
      <circle cx="19.6" cy="15.8" r="1.1" fill="#31459B" />
    </svg>
  );
}

export function GmxIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <rect x="4" y="5" width="16" height="14" rx="2" fill="#1C449B" />
      <path d="M6.8 15.95V8.1H8.84L11.17 12.12L13.5 8.1H15.55V15.95H13.73V11.24L11.78 14.6H10.56L8.61 11.24V15.95H6.8Z" fill="#FFFFFF" />
      <path d="M16.15 8.1H18.35L15.65 11.95L18.45 15.95H16.17L14.48 13.45L12.8 15.95H10.56L13.36 11.95L10.66 8.1H12.93L14.48 10.43L16.15 8.1Z" fill="#FFFFFF" />
    </svg>
  );
}

export function YandexIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <path d="M8 5H11.08C14.31 5 16.4 6.77 16.4 9.68C16.4 11.82 15.15 13.24 13.06 13.72L17.6 19H14.54L10.46 14.08H10.24V19H8V5ZM10.24 7.02V12.14H10.97C12.95 12.14 14.14 11.22 14.14 9.62C14.14 7.98 12.97 7.02 10.98 7.02H10.24Z" fill="#FF0000" />
    </svg>
  );
}

export function NaverIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return (
    <svg {...getIconProps(className, size)} {...props}>
      <rect width="24" height="24" rx="4" fill="#03C75A" />
      <path d="M13.27 12.84L10.47 8.5H7.8V15.5H10.73V11.16L13.53 15.5H16.2V8.5H13.27V12.84Z" fill="#FFFFFF" />
    </svg>
  );
}

export function OtherProviderIcon({ className, size = 24, ...props }: ProviderIconProps) {
  return <Mail className={className} size={size} color="#6B7280" {...props} />;
}
