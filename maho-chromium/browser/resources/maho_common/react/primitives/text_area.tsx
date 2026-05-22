// Copyright 2026 Maho Browser. All rights reserved.

import {forwardRef} from 'react';
import type {TextareaHTMLAttributes} from 'react';

import {joinClassNames} from './join_class_names.js';

export const TextArea = forwardRef<HTMLTextAreaElement, TextareaHTMLAttributes<HTMLTextAreaElement>>(
    function TextArea({className, ...props}, ref) {
      return <textarea {...props} ref={ref} className={joinClassNames(className)} />;
    });

TextArea.displayName = 'TextArea';
