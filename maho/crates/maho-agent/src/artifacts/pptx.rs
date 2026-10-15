use std::io::Write;

use anyhow::{bail, Context, Result};
use base64::Engine;
use serde::Deserialize;
use zip::write::SimpleFileOptions;

use super::{MAX_PPTX_IMAGE_BYTES, MAX_PPTX_SLIDES};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct PresentationInput {
    slides: Vec<Slide>,
}

#[derive(Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
enum Slide {
    Title {
        title: String,
        #[serde(default)]
        subtitle: Option<String>,
    },
    Bullets {
        title: String,
        bullets: Vec<String>,
    },
    Image {
        #[serde(default)]
        title: Option<String>,
        image_base64: String,
    },
}

const MAX_BULLETS: usize = 50;
const MAX_TEXT_CHARS: usize = 2_000;
const SLIDE_CX: i64 = 12_192_000;
const SLIDE_CY: i64 = 6_858_000;
const EMU_PER_PIXEL: i64 = 9_525;

struct Image {
    bytes: Vec<u8>,
    extension: &'static str,
    width: u32,
    height: u32,
}

pub(super) fn create(input: &[u8]) -> Result<Vec<u8>> {
    let parsed: PresentationInput = serde_json::from_slice(input).context("parse pptx input")?;
    if parsed.slides.is_empty() || parsed.slides.len() > MAX_PPTX_SLIDES {
        bail!("presentation needs between 1 and {MAX_PPTX_SLIDES} slides");
    }

    let mut slides = Vec::with_capacity(parsed.slides.len());
    let mut images: Vec<Option<Image>> = Vec::with_capacity(parsed.slides.len());
    for slide in &parsed.slides {
        let (xml, image) = match slide {
            Slide::Title { title, subtitle } => {
                let mut shapes = vec![text_shape(
                    2,
                    "Title",
                    1_524_000,
                    2_130_000,
                    9_144_000,
                    1_470_000,
                    &[checked(title)?],
                    4_400,
                    true,
                )];
                if let Some(subtitle) = subtitle {
                    shapes.push(text_shape(
                        3,
                        "Subtitle",
                        1_524_000,
                        3_800_000,
                        9_144_000,
                        1_000_000,
                        &[checked(subtitle)?],
                        2_400,
                        false,
                    ));
                }
                (slide_xml(&shapes.concat()), None)
            }
            Slide::Bullets { title, bullets } => {
                if bullets.is_empty() || bullets.len() > MAX_BULLETS {
                    bail!("a bullet slide needs between 1 and {MAX_BULLETS} bullets");
                }
                let bullets = bullets
                    .iter()
                    .map(|b| checked(b))
                    .collect::<Result<Vec<_>>>()?;
                let shapes = [
                    text_shape(
                        2,
                        "Title",
                        838_200,
                        365_125,
                        10_515_600,
                        1_325_563,
                        &[checked(title)?],
                        3_600,
                        true,
                    ),
                    bullet_shape(3, 838_200, 1_825_625, 10_515_600, 4_351_338, &bullets),
                ]
                .concat();
                (slide_xml(&shapes), None)
            }
            Slide::Image {
                title,
                image_base64,
            } => {
                let image = decode_image(image_base64)?;
                let top: i64 = if title.is_some() { 1_500_000 } else { 457_200 };
                let (cx, cy) = fit(
                    image.width,
                    image.height,
                    SLIDE_CX - 914_400,
                    SLIDE_CY - top - 457_200,
                );
                let mut shapes = String::new();
                if let Some(title) = title {
                    shapes.push_str(&text_shape(
                        2,
                        "Title",
                        838_200,
                        365_125,
                        10_515_600,
                        1_000_000,
                        &[checked(title)?],
                        3_200,
                        true,
                    ));
                }
                shapes.push_str(&picture_shape(3, (SLIDE_CX - cx) / 2, top, cx, cy));
                (slide_xml(&shapes), Some(image))
            }
        };
        slides.push(xml);
        images.push(image);
    }

    let mut zip = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let options = SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    let mut put = |name: &str, body: &[u8]| -> Result<()> {
        zip.start_file(name, options)
            .with_context(|| format!("start {name}"))?;
        zip.write_all(body)
            .with_context(|| format!("write {name}"))?;
        Ok(())
    };

    put("[Content_Types].xml", content_types(&images).as_bytes())?;
    put("_rels/.rels", ROOT_RELS.as_bytes())?;
    put("docProps/app.xml", APP_XML.as_bytes())?;
    put("docProps/core.xml", CORE_XML.as_bytes())?;
    put(
        "ppt/presentation.xml",
        presentation_xml(slides.len()).as_bytes(),
    )?;
    put(
        "ppt/_rels/presentation.xml.rels",
        presentation_rels(slides.len()).as_bytes(),
    )?;
    put("ppt/slideMasters/slideMaster1.xml", SLIDE_MASTER.as_bytes())?;
    put(
        "ppt/slideMasters/_rels/slideMaster1.xml.rels",
        SLIDE_MASTER_RELS.as_bytes(),
    )?;
    put("ppt/slideLayouts/slideLayout1.xml", SLIDE_LAYOUT.as_bytes())?;
    put(
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels",
        SLIDE_LAYOUT_RELS.as_bytes(),
    )?;
    put("ppt/theme/theme1.xml", THEME.as_bytes())?;
    for (index, (xml, image)) in slides.iter().zip(&images).enumerate() {
        let number = index + 1;
        put(&format!("ppt/slides/slide{number}.xml"), xml.as_bytes())?;
        let media = image
            .as_ref()
            .map(|img| format!("image{number}.{}", img.extension));
        put(
            &format!("ppt/slides/_rels/slide{number}.xml.rels"),
            slide_rels(media.as_deref()).as_bytes(),
        )?;
        if let (Some(name), Some(image)) = (media, image) {
            put(&format!("ppt/media/{name}"), &image.bytes)?;
        }
    }
    Ok(zip.finish().context("finish pptx archive")?.into_inner())
}

fn checked(text: &str) -> Result<String> {
    if text.chars().count() > MAX_TEXT_CHARS {
        bail!("slide text exceeds {MAX_TEXT_CHARS} characters");
    }
    Ok(escape(text))
}

fn escape(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    for ch in text.chars() {
        match ch {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&apos;"),
            '\t' | '\n' | '\r' => out.push(' '),
            ch if ch.is_control() => {}
            ch => out.push(ch),
        }
    }
    out
}

fn decode_image(encoded: &str) -> Result<Image> {
    if encoded.len() > MAX_PPTX_IMAGE_BYTES * 4 / 3 + 4 {
        bail!("image exceeds {MAX_PPTX_IMAGE_BYTES} bytes");
    }
    let bytes = base64::engine::general_purpose::STANDARD
        .decode(encoded.trim())
        .context("image is not valid base64")?;
    if bytes.len() > MAX_PPTX_IMAGE_BYTES {
        bail!("image exceeds {MAX_PPTX_IMAGE_BYTES} bytes");
    }
    let (extension, width, height) = if let Some((w, h)) = png_size(&bytes) {
        ("png", w, h)
    } else if let Some((w, h)) = jpeg_size(&bytes) {
        ("jpeg", w, h)
    } else {
        bail!("image must be a PNG or JPEG");
    };
    if width == 0 || height == 0 || width > 20_000 || height > 20_000 {
        bail!("image dimensions {width}x{height} are out of range");
    }
    Ok(Image {
        bytes,
        extension,
        width,
        height,
    })
}

fn png_size(bytes: &[u8]) -> Option<(u32, u32)> {
    const SIGNATURE: &[u8] = b"\x89PNG\r\n\x1a\n";
    if bytes.len() < 24 || !bytes.starts_with(SIGNATURE) || &bytes[12..16] != b"IHDR" {
        return None;
    }
    let width = u32::from_be_bytes(bytes[16..20].try_into().ok()?);
    let height = u32::from_be_bytes(bytes[20..24].try_into().ok()?);
    Some((width, height))
}

fn jpeg_size(bytes: &[u8]) -> Option<(u32, u32)> {
    if !bytes.starts_with(&[0xFF, 0xD8]) {
        return None;
    }
    let mut pos = 2;
    while pos + 4 <= bytes.len() {
        if bytes[pos] != 0xFF {
            return None;
        }
        let marker = bytes[pos + 1];
        if marker == 0xFF {
            pos += 1;
            continue;
        }
        let length = u16::from_be_bytes([bytes[pos + 2], bytes[pos + 3]]) as usize;
        if length < 2 {
            return None;
        }
        let is_frame = matches!(marker, 0xC0..=0xCF) && !matches!(marker, 0xC4 | 0xC8 | 0xCC);
        if is_frame {
            let frame = bytes.get(pos + 4..pos + 9)?;
            let height = u16::from_be_bytes([frame[1], frame[2]]) as u32;
            let width = u16::from_be_bytes([frame[3], frame[4]]) as u32;
            return Some((width, height));
        }
        pos += 2 + length;
    }
    None
}

fn fit(width: u32, height: u32, max_cx: i64, max_cy: i64) -> (i64, i64) {
    let cx = width as i64 * EMU_PER_PIXEL;
    let cy = height as i64 * EMU_PER_PIXEL;
    let scale = f64::min(
        1.0,
        f64::min(max_cx as f64 / cx as f64, max_cy as f64 / cy as f64),
    );
    (((cx as f64) * scale) as i64, ((cy as f64) * scale) as i64)
}

#[allow(clippy::too_many_arguments)]
fn text_shape(
    id: u32,
    name: &str,
    x: i64,
    y: i64,
    cx: i64,
    cy: i64,
    paragraphs: &[String],
    size: u32,
    bold: bool,
) -> String {
    let bold = if bold { " b=\"1\"" } else { "" };
    let body: String = paragraphs
        .iter()
        .map(|text| format!(r#"<a:p><a:r><a:rPr lang="en-US" sz="{size}"{bold} dirty="0"/><a:t>{text}</a:t></a:r></a:p>"#))
        .collect();
    format!(
        r#"<p:sp><p:nvSpPr><p:cNvPr id="{id}" name="{name}"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/></p:spPr><p:txBody><a:bodyPr wrap="square" rtlCol="0"><a:normAutofit/></a:bodyPr><a:lstStyle/>{body}</p:txBody></p:sp>"#
    )
}

fn bullet_shape(id: u32, x: i64, y: i64, cx: i64, cy: i64, bullets: &[String]) -> String {
    let body: String = bullets
        .iter()
        .map(|text| format!(r#"<a:p><a:pPr marL="342900" indent="-342900"><a:buFont typeface="Arial"/><a:buChar char="&#8226;"/></a:pPr><a:r><a:rPr lang="en-US" sz="2400" dirty="0"/><a:t>{text}</a:t></a:r></a:p>"#))
        .collect();
    format!(
        r#"<p:sp><p:nvSpPr><p:cNvPr id="{id}" name="Bullets"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/></p:spPr><p:txBody><a:bodyPr wrap="square" rtlCol="0"><a:normAutofit/></a:bodyPr><a:lstStyle/>{body}</p:txBody></p:sp>"#
    )
}

fn picture_shape(id: u32, x: i64, y: i64, cx: i64, cy: i64) -> String {
    format!(
        r#"<p:pic><p:nvPicPr><p:cNvPr id="{id}" name="Picture"/><p:cNvPicPr><a:picLocks noChangeAspect="1"/></p:cNvPicPr><p:nvPr/></p:nvPicPr><p:blipFill><a:blip r:embed="rId2"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr></p:pic>"#
    )
}

const NS: &str = r#"xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main""#;
const XML_DECL: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?>"#;
const EMPTY_GROUP: &str = r#"<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>"#;

fn slide_xml(shapes: &str) -> String {
    format!(
        r#"{XML_DECL}<p:sld {NS}><p:cSld><p:spTree>{EMPTY_GROUP}{shapes}</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>"#
    )
}

fn slide_rels(media: Option<&str>) -> String {
    let image = media
        .map(|name| format!(r#"<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/{name}"/>"#))
        .unwrap_or_default();
    format!(
        r#"{XML_DECL}<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout" Target="../slideLayouts/slideLayout1.xml"/>{image}</Relationships>"#
    )
}

fn content_types(images: &[Option<Image>]) -> String {
    let mut defaults = String::from(
        r#"<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/>"#,
    );
    if images.iter().flatten().any(|img| img.extension == "png") {
        defaults.push_str(r#"<Default Extension="png" ContentType="image/png"/>"#);
    }
    if images.iter().flatten().any(|img| img.extension == "jpeg") {
        defaults.push_str(r#"<Default Extension="jpeg" ContentType="image/jpeg"/>"#);
    }
    let slides: String = (1..=images.len())
        .map(|n| format!(r#"<Override PartName="/ppt/slides/slide{n}.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>"#))
        .collect();
    format!(
        r#"{XML_DECL}<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">{defaults}<Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/><Override PartName="/ppt/slideMasters/slideMaster1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml"/><Override PartName="/ppt/slideLayouts/slideLayout1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml"/><Override PartName="/ppt/theme/theme1.xml" ContentType="application/vnd.openxmlformats-officedocument.theme+xml"/><Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/><Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>{slides}</Types>"#
    )
}

fn presentation_xml(slide_count: usize) -> String {
    let ids: String = (1..=slide_count)
        .map(|n| format!(r#"<p:sldId id="{}" r:id="rId{}"/>"#, 255 + n, n + 2))
        .collect();
    format!(
        r#"{XML_DECL}<p:presentation {NS} saveSubsetFonts="1"><p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst><p:sldIdLst>{ids}</p:sldIdLst><p:sldSz cx="{SLIDE_CX}" cy="{SLIDE_CY}"/><p:notesSz cx="6858000" cy="9144000"/></p:presentation>"#
    )
}

fn presentation_rels(slide_count: usize) -> String {
    let slides: String = (1..=slide_count)
        .map(|n| format!(r#"<Relationship Id="rId{}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="slides/slide{n}.xml"/>"#, n + 2))
        .collect();
    format!(
        r#"{XML_DECL}<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster" Target="slideMasters/slideMaster1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme" Target="theme/theme1.xml"/>{slides}</Relationships>"#
    )
}

const ROOT_RELS: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="ppt/presentation.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/></Relationships>"#;

const APP_XML: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"><Application>Maho</Application></Properties>"#;

const CORE_XML: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:creator>Maho</dc:creator></cp:coreProperties>"#;

const SLIDE_MASTER: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><p:sldMaster xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"><p:cSld><p:bg><p:bgRef idx="1001"><a:schemeClr val="bg1"/></p:bgRef></p:bg><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr></p:spTree></p:cSld><p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/><p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst></p:sldMaster>"#;

const SLIDE_MASTER_RELS: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout" Target="../slideLayouts/slideLayout1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme" Target="../theme/theme1.xml"/></Relationships>"#;

const SLIDE_LAYOUT: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><p:sldLayout xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" type="blank" preserve="1"><p:cSld name="Blank"><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr></p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>"#;

const SLIDE_LAYOUT_RELS: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster" Target="../slideMasters/slideMaster1.xml"/></Relationships>"#;

const THEME: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Maho"><a:themeElements><a:clrScheme name="Maho"><a:dk1><a:srgbClr val="000000"/></a:dk1><a:lt1><a:srgbClr val="FFFFFF"/></a:lt1><a:dk2><a:srgbClr val="221B4E"/></a:dk2><a:lt2><a:srgbClr val="EEECE1"/></a:lt2><a:accent1><a:srgbClr val="7C3AED"/></a:accent1><a:accent2><a:srgbClr val="4F81BD"/></a:accent2><a:accent3><a:srgbClr val="9BBB59"/></a:accent3><a:accent4><a:srgbClr val="8064A2"/></a:accent4><a:accent5><a:srgbClr val="4BACC6"/></a:accent5><a:accent6><a:srgbClr val="F79646"/></a:accent6><a:hlink><a:srgbClr val="0000FF"/></a:hlink><a:folHlink><a:srgbClr val="800080"/></a:folHlink></a:clrScheme><a:fontScheme name="Maho"><a:majorFont><a:latin typeface="Calibri"/><a:ea typeface=""/><a:cs typeface=""/></a:majorFont><a:minorFont><a:latin typeface="Calibri"/><a:ea typeface=""/><a:cs typeface=""/></a:minorFont></a:fontScheme><a:fmtScheme name="Maho"><a:fillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:fillStyleLst><a:lnStyleLst><a:ln w="9525"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln><a:ln w="25400"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln><a:ln w="38100"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln></a:lnStyleLst><a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst><a:bgFillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>"#;
