pub fn sanitize_font_family(font_family: &str) -> Option<String> {
    if font_family.len() > 256 {
        return None;
    }
    let mut quote = None;
    for character in font_family.chars() {
        if !(character.is_ascii_alphanumeric()
            || matches!(character, ' ' | ',' | '-' | '_' | '\'' | '"'))
        {
            return None;
        }
        if matches!(character, '\'' | '"') {
            match quote {
                Some(delimiter) if delimiter == character => quote = None,
                None => quote = Some(character),
                Some(_) => {}
            }
        }
    }
    quote.is_none().then(|| font_family.to_string())
}

pub fn sanitize_custom_css(stylesheet: &str) -> String {
    if stylesheet.len() > 256 * 1024 {
        return String::new();
    }
    if stylesheet.contains('\0') {
        return String::new();
    }
    let characters: Vec<char> = stylesheet.chars().collect();
    let mut declaration_heads = Vec::new();
    let mut index = 0;
    while index < characters.len() {
        match characters[index] {
            '/' if characters.get(index + 1) == Some(&'*') => match trivia(&characters, index) {
                Some(next) => index = next,
                None => return String::new(),
            },
            '\'' | '"' => {
                if let Some(declaration_head) = declaration_heads.last_mut() {
                    *declaration_head = false;
                }
                match string(&characters, index) {
                    Some(next) => index = next,
                    None => return String::new(),
                }
            }
            '{' => {
                declaration_heads.push(true);
                index += 1;
            }
            '}' => {
                if declaration_heads.pop().is_none() {
                    return String::new();
                }
                if let Some(declaration_head) = declaration_heads.last_mut() {
                    *declaration_head = true;
                }
                index += 1;
            }
            ';' => {
                if let Some(declaration_head) = declaration_heads.last_mut() {
                    *declaration_head = true;
                }
                index += 1;
            }
            '@' => {
                if let Some(declaration_head) = declaration_heads.last_mut() {
                    *declaration_head = false;
                }
                let Some(at_name_index) = trivia(&characters, index + 1) else {
                    return String::new();
                };
                match identifier(&characters, at_name_index) {
                    Some((name, _))
                        if name.eq_ignore_ascii_case("import")
                            || name.eq_ignore_ascii_case("charset") =>
                    {
                        return String::new()
                    }
                    Some((_, next)) => index = next,
                    None => index += 1,
                }
            }
            character if ident_start(character) => {
                let Some((name, end)) = identifier(&characters, index) else {
                    return String::new();
                };
                let at_declaration_head = declaration_heads.last().copied().unwrap_or(false);
                let Some(after_ident) = trivia(&characters, end) else {
                    return String::new();
                };
                if characters.get(after_ident) == Some(&'(') {
                    if name.eq_ignore_ascii_case("expression") {
                        return String::new();
                    }
                    if name.eq_ignore_ascii_case("image-set")
                        || name.eq_ignore_ascii_case("-webkit-image-set")
                    {
                        return String::new();
                    }
                    if name.eq_ignore_ascii_case("url") {
                        match url(&characters, after_ident + 1) {
                            Some((after, false)) => {
                                if let Some(declaration_head) = declaration_heads.last_mut() {
                                    *declaration_head = false;
                                }
                                index = after;
                                continue;
                            }
                            _ => return String::new(),
                        }
                    }
                }
                if characters.get(after_ident) == Some(&':')
                    && at_declaration_head
                    && (name.eq_ignore_ascii_case("behavior")
                        || name.eq_ignore_ascii_case("-moz-binding"))
                    && declaration_before_block(&characters, after_ident + 1).unwrap_or(true)
                {
                    return String::new();
                }
                if let Some(declaration_head) = declaration_heads.last_mut() {
                    *declaration_head = false;
                }
                index = end;
            }
            character => {
                if !character.is_ascii_whitespace() {
                    if let Some(declaration_head) = declaration_heads.last_mut() {
                        *declaration_head = false;
                    }
                }
                index += 1;
            }
        }
    }
    if declaration_heads.is_empty() {
        stylesheet.to_string()
    } else {
        String::new()
    }
}

fn trivia(characters: &[char], mut index: usize) -> Option<usize> {
    loop {
        while characters
            .get(index)
            .is_some_and(|c| c.is_ascii_whitespace())
        {
            index += 1;
        }
        if characters.get(index) != Some(&'/') || characters.get(index + 1) != Some(&'*') {
            return Some(index);
        }
        index += 2;
        while characters.get(index..index + 2) != Some(&['*', '/']) {
            if characters.get(index).is_none() {
                return None;
            }
            index += 1;
        }
        index += 2;
    }
}

fn string(characters: &[char], mut index: usize) -> Option<usize> {
    let quote = *characters.get(index)?;
    index += 1;
    while let Some(character) = characters.get(index) {
        match character {
            '\\' => index = escape(characters, index)?.1,
            _ if *character == quote => return Some(index + 1),
            '\n' | '\r' | '\u{000C}' => return None,
            _ => index += 1,
        }
    }
    None
}

fn identifier(characters: &[char], mut index: usize) -> Option<(String, usize)> {
    let mut name = String::new();
    while let Some(character) = characters.get(index) {
        if *character == '\\' {
            let (value, next) = escape(characters, index)?;
            name.push(value?);
            index = next;
        } else if ident_part(*character) {
            name.push(*character);
            index += 1;
        } else {
            break;
        }
    }
    (!name.is_empty()).then_some((name, index))
}

fn declaration_before_block(characters: &[char], mut index: usize) -> Option<bool> {
    let mut parentheses = 0_u32;
    let mut brackets = 0_u32;
    loop {
        match *characters.get(index)? {
            '/' if characters.get(index + 1) == Some(&'*') => {
                index = trivia(characters, index)?;
            }
            '\'' | '"' => index = string(characters, index)?,
            '\\' => index = escape(characters, index)?.1,
            '(' => {
                parentheses += 1;
                index += 1;
            }
            ')' => {
                parentheses = parentheses.saturating_sub(1);
                index += 1;
            }
            '[' => {
                brackets += 1;
                index += 1;
            }
            ']' => {
                brackets = brackets.saturating_sub(1);
                index += 1;
            }
            '{' if parentheses == 0 && brackets == 0 => return Some(false),
            ';' | '}' if parentheses == 0 && brackets == 0 => return Some(true),
            _ => index += 1,
        }
    }
}

fn url(characters: &[char], mut index: usize) -> Option<(usize, bool)> {
    index = trivia(characters, index)?;
    let quote = matches!(characters.get(index), Some('\'' | '"')).then(|| characters[index]);
    if quote.is_some() {
        index += 1;
    }
    let mut value = String::new();
    loop {
        let character = *characters.get(index)?;
        if quote.is_some_and(|delimiter| character == delimiter) {
            index = trivia(characters, index + 1)?;
            return (characters.get(index) == Some(&')')).then(|| (index + 1, dangerous(&value)));
        }
        if quote.is_none() && character == ')' {
            return Some((index + 1, dangerous(&value)));
        }
        if quote.is_none()
            && (character.is_ascii_whitespace() || matches!(character, '\'' | '"' | '('))
        {
            return None;
        }
        if character == '\\' {
            let (decoded, next) = escape(characters, index)?;
            if let Some(decoded) = decoded {
                if decoded.is_control() || decoded == '\u{007F}' {
                    return None;
                }
                value.push(decoded);
            }
            index = next;
        } else if matches!(character, '\n' | '\r' | '\u{000C}') {
            return None;
        } else {
            if character.is_control() || character == '\u{007F}' {
                return None;
            }
            value.push(character);
            index += 1;
        }
    }
}

fn escape(characters: &[char], index: usize) -> Option<(Option<char>, usize)> {
    let first = *characters.get(index + 1)?;
    if matches!(first, '\n' | '\u{000C}') {
        return Some((None, index + 2));
    }
    if first == '\r' {
        return Some((
            None,
            index + 2 + usize::from(characters.get(index + 2) == Some(&'\n')),
        ));
    }
    if !first.is_ascii_hexdigit() {
        return Some((Some(first), index + 2));
    }
    let mut end = index + 1;
    while end < characters.len() && end < index + 7 && characters[end].is_ascii_hexdigit() {
        end += 1;
    }
    let hex = characters[index + 1..end].iter().collect::<String>();
    let decoded = char::from_u32(u32::from_str_radix(&hex, 16).ok()?).unwrap_or('\u{FFFD}');
    if characters.get(end).is_some_and(|c| c.is_ascii_whitespace()) {
        end += 1;
    }
    Some((Some(decoded), end))
}

fn dangerous(value: &str) -> bool {
    let value = value.trim().to_ascii_lowercase();
    if value.starts_with('#') {
        return false;
    }
    if let Some(media_type) = value.strip_prefix("data:") {
        let media_type = media_type.trim_start();
        return !(media_type.starts_with("image/png;base64,")
            || media_type.starts_with("image/jpeg;base64,")
            || media_type.starts_with("image/gif;base64,")
            || media_type.starts_with("image/webp;base64,")
            || media_type.starts_with("image/avif;base64,"));
    }
    true
}
fn ident_start(character: char) -> bool {
    character == '\\' || ident_part(character)
}
fn ident_part(character: char) -> bool {
    character.is_ascii_alphanumeric() || matches!(character, '-' | '_') || !character.is_ascii()
}
