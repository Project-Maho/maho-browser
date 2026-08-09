package dev.maho.browser.support

import dev.maho.browser.models.ExtractedArticle
import dev.maho.browser.models.ReaderSettings
import dev.maho.browser.models.ReaderTheme

fun renderReaderHtml(article: ExtractedArticle, settings: ReaderSettings): String {
    val (backgroundColor, foregroundColor) = when (settings.theme) {
        ReaderTheme.Sepia -> "#faf2df" to "#5a402a"
        ReaderTheme.Dark -> "#1e1e1e" to "#dcdcdc"
        ReaderTheme.Light -> "#ffffff" to "#000000"
    }
    val fontStack = when (settings.fontFamily) {
        "Georgia" -> "Georgia, serif"
        "Courier New" -> "'Courier New', monospace"
        else -> "sans-serif"
    }
    val bylineColor = if (settings.theme == ReaderTheme.Dark) "#888" else "#666"
    val byline = if (article.byline.isEmpty()) {
        ""
    } else {
        "<p class=\"byline\">By ${article.byline}</p>"
    }

    return """
        <!DOCTYPE html>
        <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <style>
                body {
                    background-color: $backgroundColor;
                    color: $foregroundColor;
                    font-family: $fontStack;
                    font-size: ${settings.fontSize}px;
                    line-height: 1.6;
                    margin: 0;
                    padding: 16px;
                }
                .container {
                    max-width: 600px;
                    margin: 0 auto;
                }
                .title {
                    font-size: 1.5em;
                    margin-bottom: 8px;
                    line-height: 1.2;
                }
                .byline {
                    font-size: 0.9em;
                    color: $bylineColor;
                    margin-bottom: 24px;
                    font-style: italic;
                }
                img {
                    max-width: 100%;
                    height: auto;
                    border-radius: 8px;
                    margin: 16px 0;
                }
            </style>
        </head>
        <body>
            <div class="container">
                <h1 class="title">${article.title}</h1>
                $byline
                <div class="content">
                    ${article.content}
                </div>
            </div>
        </body>
        </html>
    """.trimIndent()
}
