// Copyright 2026 Maho Browser. All rights reserved.

import {forwardRef} from 'react';
import type {InputHTMLAttributes} from 'react';

import {joinClassNames} from './join_class_names.js';

export const TextInput = forwardRef<HTMLInputElement, InputHTMLAttributes<HTMLInputElement>>(
    function TextInput({className, ...props}, ref) {
      return <input {...props} ref={ref} className={joinClassNames(className)} />;
    });

TextInput.displayName = 'TextInput';
