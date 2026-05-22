// Copyright 2026 Maho Browser. All rights reserved.

import {forwardRef} from 'react';
import type {ButtonHTMLAttributes} from 'react';

import {joinClassNames} from './join_class_names.js';

export const Button = forwardRef<HTMLButtonElement, ButtonHTMLAttributes<HTMLButtonElement>>(
    function Button({className, ...props}, ref) {
      return <button {...props} ref={ref} className={joinClassNames(className)} />;
    });

Button.displayName = 'Button';
