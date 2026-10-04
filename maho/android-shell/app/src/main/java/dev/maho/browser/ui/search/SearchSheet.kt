package dev.maho.browser.ui.search

import android.graphics.BitmapFactory
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.selection.LocalTextSelectionColors
import androidx.compose.foundation.text.selection.TextSelectionColors
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.maho.browser.bridge.BridgeCommandBar
import dev.maho.browser.models.ImageData
import dev.maho.browser.models.SuggestionType
import dev.maho.browser.models.SuggestionViewModel
import dev.maho.browser.support.VoiceSearchButton
import dev.maho.browser.support.VoiceSearchManager
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellColors
import dev.maho.browser.ui.theme.BrowserShellTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlin.math.min

fun interface SearchSuggestionProvider {
    suspend fun suggestions(query: String, isIncognito: Boolean): List<SuggestionViewModel>
}

object BridgeSearchSuggestionProvider : SearchSuggestionProvider {
    override suspend fun suggestions(query: String, isIncognito: Boolean): List<SuggestionViewModel> =
        withContext(Dispatchers.IO) {
            BridgeCommandBar.suggestions(
                BridgeCommandBar.query(text = query, isIncognito = isIncognito),
            ).orEmpty()
        }
}

@Composable
fun SearchSheet(
    initialQuery: String,
    isIncognito: Boolean,
    onDismiss: () -> Unit = {},
    onSubmit: (String) -> Unit,
    onToggleIncognito: () -> Unit,
    onSelectSuggestion: (SuggestionViewModel) -> Unit,
    onBrowseForMe: (String) -> Unit,
    suggestionProvider: SearchSuggestionProvider = BridgeSearchSuggestionProvider,
    onRecordSuggestionSelection: (Int, SuggestionViewModel) -> Unit = { _, _ -> },
    requestKeyboardOnAppear: Boolean = true,
) {
    val shellColors = BrowserShellTheme.colors
    val context = LocalContext.current
    val keyboardController = LocalSoftwareKeyboardController.current
    val voiceSearchManager = remember(context) { VoiceSearchManager(context) }
    val focusRequester = remember { FocusRequester() }
    val isDarkMode = isSystemInDarkTheme()

    var query by remember(initialQuery) { mutableStateOf(initialQuery) }

    LaunchedEffect(initialQuery) {
        query = initialQuery
    }
    var suggestions by remember { mutableStateOf<List<SuggestionViewModel>>(emptyList()) }
    var requestSerial by remember { mutableIntStateOf(0) }
    var voiceRefocusSerial by remember { mutableIntStateOf(0) }
    val colors = searchSheetColors(
        shellColors = shellColors,
        isIncognito = isIncognito,
        isDarkMode = isDarkMode,
    )

    DisposableEffect(voiceSearchManager) {
        onDispose {
            voiceSearchManager.reset()
        }
    }

    LaunchedEffect(query, isIncognito, suggestionProvider) {
        val requestId = requestSerial + 1
        requestSerial = requestId
        val requestQuery = query
        delay(150)
        val nextSuggestions = suggestionProvider.suggestions(
            query = requestQuery,
            isIncognito = isIncognito,
        )
        if (requestSerial == requestId && query == requestQuery) {
            suggestions = nextSuggestions
        }
    }

    LaunchedEffect(Unit) {
        if (!requestKeyboardOnAppear) return@LaunchedEffect
        delay(80)
        focusRequester.requestFocus()
        keyboardController?.show()
    }

    LaunchedEffect(voiceRefocusSerial) {
        if (voiceRefocusSerial == 0) return@LaunchedEffect
        delay(100)
        voiceSearchManager.reset()
        focusRequester.requestFocus()
        keyboardController?.show()
    }

    BackHandler(onBack = onDismiss)

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .testTag("searchSheet"),
        contentAlignment = Alignment.BottomCenter,
    ) {
        val panelHeight = min(maxHeight.value * 0.5f, SearchPanelMaxHeight.value).dp
        val scrimInteraction = remember { MutableInteractionSource() }

        Box(
            modifier = Modifier
                .matchParentSize()
                .background(Color.Black.copy(alpha = 0.58f))
                .clickable(
                    interactionSource = scrimInteraction,
                    indication = null,
                    role = Role.Button,
                    onClick = onDismiss,
                )
                .semantics { contentDescription = "Close" }
                .testTag("searchSheetScrim"),
        )

        SearchPanel(
            height = panelHeight,
            colors = colors,
            isIncognito = isIncognito,
            query = query,
            onQueryChange = { query = it },
            focusRequester = focusRequester,
            keyboardController = keyboardController,
            voiceSearchManager = voiceSearchManager,
            onSubmit = onSubmit,
            onToggleIncognito = onToggleIncognito,
            onVoiceTranscript = { transcript ->
                val trimmedTranscript = transcript.trim()
                if (trimmedTranscript.isNotEmpty()) {
                    query = trimmedTranscript
                    voiceRefocusSerial += 1
                }
            },
            suggestions = suggestions,
            onSelectSuggestion = { indexedSuggestion ->
                onRecordSuggestionSelection(indexedSuggestion.sourceIndex, indexedSuggestion.suggestion)
                onSelectSuggestion(indexedSuggestion.suggestion)
            },
            onBrowseForMe = onBrowseForMe,
        )
    }
}

@Composable
private fun SearchPanel(
    height: Dp,
    colors: SearchSheetColors,
    isIncognito: Boolean,
    query: String,
    onQueryChange: (String) -> Unit,
    focusRequester: FocusRequester,
    keyboardController: androidx.compose.ui.platform.SoftwareKeyboardController?,
    voiceSearchManager: VoiceSearchManager,
    onSubmit: (String) -> Unit,
    onToggleIncognito: () -> Unit,
    onVoiceTranscript: (String) -> Unit,
    suggestions: List<SuggestionViewModel>,
    onSelectSuggestion: (IndexedSuggestion) -> Unit,
    onBrowseForMe: (String) -> Unit,
) {
    val panelShape = RoundedCornerShape(
        topStart = SearchPanelCorner,
        topEnd = SearchPanelCorner,
        bottomStart = 0.dp,
        bottomEnd = 0.dp,
    )

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .widthIn(max = SearchPanelMaxWidth)
            .height(height)
            .navigationBarsPadding()
            .clip(panelShape)
            .background(colors.panelBackground)
            .border(BorderStroke(1.dp, colors.border), panelShape)
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = {},
            )
            .padding(horizontal = 16.dp)
            .padding(top = 16.dp, bottom = 20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        SearchField(
            query = query,
            onQueryChange = onQueryChange,
            isIncognito = isIncognito,
            colors = colors,
            focusRequester = focusRequester,
            keyboardController = keyboardController,
            voiceSearchManager = voiceSearchManager,
            onSubmit = onSubmit,
            onToggleIncognito = onToggleIncognito,
            onVoiceTranscript = onVoiceTranscript,
        )

        SearchContent(
            query = query,
            suggestions = suggestions,
            isIncognito = isIncognito,
            colors = colors,
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
            onSelectSuggestion = onSelectSuggestion,
            onBrowseForMe = onBrowseForMe,
        )
    }
}

@Composable
private fun SearchField(
    query: String,
    onQueryChange: (String) -> Unit,
    isIncognito: Boolean,
    colors: SearchSheetColors,
    focusRequester: FocusRequester,
    keyboardController: androidx.compose.ui.platform.SoftwareKeyboardController?,
    voiceSearchManager: VoiceSearchManager,
    onSubmit: (String) -> Unit,
    onToggleIncognito: () -> Unit,
    onVoiceTranscript: (String) -> Unit,
) {
    CompositionLocalProvider(
        LocalTextSelectionColors provides TextSelectionColors(
            handleColor = colors.accent,
            backgroundColor = colors.accent.copy(alpha = 0.28f),
        ),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(SearchFieldHeight)
                .clip(RoundedCornerShape(SearchFieldCorner))
                .background(colors.fieldBackground)
                .border(BorderStroke(1.dp, colors.border), RoundedCornerShape(SearchFieldCorner))
                .padding(start = 16.dp, end = 12.dp)
                .testTag("searchSheetField"),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Box(
                modifier = Modifier
                    .weight(1f)
                    .testTag("homeSearchField"),
                contentAlignment = Alignment.CenterStart,
            ) {
                if (query.isEmpty()) {
                    Text(
                        text = "Search…",
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium),
                        color = colors.mutedForeground,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }

                BasicTextField(
                    value = query,
                    onValueChange = onQueryChange,
                    modifier = Modifier
                        .fillMaxWidth()
                        .focusRequester(focusRequester)
                        .testTag("searchSheetTextField"),
                    singleLine = true,
                    textStyle = MaterialTheme.typography.titleMedium.copy(
                        color = colors.foreground,
                        fontWeight = FontWeight.Medium,
                    ),
                    keyboardOptions = KeyboardOptions(
                        capitalization = KeyboardCapitalization.None,
                        autoCorrect = false,
                        keyboardType = KeyboardType.Uri,
                        imeAction = ImeAction.Search,
                    ),
                    keyboardActions = KeyboardActions(
                        onSearch = {
                            val trimmed = query.trim()
                            if (trimmed.isNotEmpty()) {
                                keyboardController?.hide()
                                onSubmit(trimmed)
                            }
                        },
                    ),
                    cursorBrush = SolidColor(colors.accent),
                )
            }

            TrailingFieldControls(
                isIncognito = isIncognito,
                colors = colors,
                voiceSearchManager = voiceSearchManager,
                onToggleIncognito = onToggleIncognito,
                onVoiceTranscript = onVoiceTranscript,
            )
        }
    }
}

@Composable
private fun TrailingFieldControls(
    isIncognito: Boolean,
    colors: SearchSheetColors,
    voiceSearchManager: VoiceSearchManager,
    onToggleIncognito: () -> Unit,
    onVoiceTranscript: (String) -> Unit,
) {
    if (isIncognito) {
        Row(
            modifier = Modifier
                .height(44.dp)
                .clip(RoundedCornerShape(999.dp))
                .clickable(
                    role = Role.Button,
                    onClick = onToggleIncognito,
                )
                .padding(start = 8.dp, end = 4.dp)
                .semantics { contentDescription = "Exit Incognito" }
                .testTag("searchSheetPrivateToggle"),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text(
                text = "Incognito",
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = colors.mutedForeground,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Icon(
                painter = painterResource(id = MahoIcon.EyeOff.drawableRes),
                contentDescription = null,
                modifier = Modifier.size(17.dp),
                tint = colors.mutedForeground,
            )
        }
    } else {
        VoiceSearchButton(
            manager = voiceSearchManager,
            onResult = onVoiceTranscript,
            modifier = Modifier
                .size(44.dp)
                .testTag("searchSheetVoiceButton"),
            compact = true,
            buttonModifier = Modifier
                .size(44.dp)
                .testTag("searchSheetMicButton"),
            iconModifier = Modifier.size(17.dp),
            idleTint = colors.mutedForeground,
            listeningTint = colors.accent,
            errorTint = MaterialTheme.colorScheme.error,
        )

        Box(
            modifier = Modifier
                .size(44.dp)
                .clip(CircleShape)
                .clickable(
                    role = Role.Button,
                    onClick = onToggleIncognito,
                )
                .semantics { contentDescription = "Enter Incognito" }
                .testTag("searchSheetPrivateToggle"),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(id = MahoIcon.Eye.drawableRes),
                contentDescription = null,
                modifier = Modifier.size(17.dp),
                tint = colors.mutedForeground,
            )
        }
    }
}

@Composable
private fun SearchContent(
    query: String,
    suggestions: List<SuggestionViewModel>,
    isIncognito: Boolean,
    colors: SearchSheetColors,
    modifier: Modifier,
    onSelectSuggestion: (IndexedSuggestion) -> Unit,
    onBrowseForMe: (String) -> Unit,
) {
    val trimmedQuery = query.trim()
    val indexedSuggestions = suggestions.mapIndexed { index, suggestion ->
        IndexedSuggestion(sourceIndex = index, suggestion = suggestion)
    }
    val primarySuggestions = if (trimmedQuery.isEmpty()) {
        emptyList()
    } else {
        indexedSuggestions.filter { indexed ->
            indexed.suggestion.kind != SuggestionType.Action && indexed.suggestion.kind != SuggestionType.History
        }
    }
    val historySuggestions = if (trimmedQuery.isEmpty()) {
        emptyList()
    } else {
        indexedSuggestions.filter { it.suggestion.kind == SuggestionType.History }
    }
    val actionSuggestions = if (trimmedQuery.isEmpty()) {
        emptyList()
    } else {
        indexedSuggestions.filter { it.suggestion.kind == SuggestionType.Action }
    }
    val hasVisibleSuggestions = primarySuggestions.isNotEmpty() || historySuggestions.isNotEmpty() || actionSuggestions.isNotEmpty()

    when {
        isIncognito && trimmedQuery.isEmpty() -> IncognitoEmptyState(colors = colors, modifier = modifier)
        hasVisibleSuggestions -> SuggestionsList(
            query = trimmedQuery,
            primarySuggestions = primarySuggestions,
            historySuggestions = historySuggestions,
            actionSuggestions = actionSuggestions,
            colors = colors,
            modifier = modifier,
            onSelectSuggestion = onSelectSuggestion,
            onBrowseForMe = onBrowseForMe,
        )
        else -> EmptySuggestionsState(query = trimmedQuery, colors = colors, modifier = modifier)
    }
}

@Composable
private fun IncognitoEmptyState(colors: SearchSheetColors, modifier: Modifier) {
    Box(
        modifier = modifier.testTag("searchSheetIncognitoEmptyState"),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = "You're browsing Incognito",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            color = colors.mutedForeground,
        )
    }
}

@Composable
private fun EmptySuggestionsState(query: String, colors: SearchSheetColors, modifier: Modifier) {
    Box(
        modifier = modifier.testTag("searchSheetEmptyState"),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = if (query.isEmpty()) "Search or enter a website" else "Press Search to go",
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            color = colors.mutedForeground,
        )
    }
}

@Composable
private fun SuggestionsList(
    query: String,
    primarySuggestions: List<IndexedSuggestion>,
    historySuggestions: List<IndexedSuggestion>,
    actionSuggestions: List<IndexedSuggestion>,
    colors: SearchSheetColors,
    modifier: Modifier,
    onSelectSuggestion: (IndexedSuggestion) -> Unit,
    onBrowseForMe: (String) -> Unit,
) {
    LazyColumn(
        modifier = modifier.testTag("searchSheetSuggestions"),
        verticalArrangement = Arrangement.Top,
    ) {
        item {
            Spacer(modifier = Modifier.height(6.dp))
        }

        suggestionRows(
            suggestions = primarySuggestions,
            query = query,
            colors = colors,
            onSelectSuggestion = onSelectSuggestion,
            onBrowseForMe = onBrowseForMe,
        )

        suggestionSection(
            title = "History",
            suggestions = historySuggestions,
            query = query,
            colors = colors,
            onSelectSuggestion = onSelectSuggestion,
            onBrowseForMe = onBrowseForMe,
        )

        suggestionSection(
            title = "Commands",
            suggestions = actionSuggestions,
            query = query,
            colors = colors,
            onSelectSuggestion = onSelectSuggestion,
            onBrowseForMe = onBrowseForMe,
        )
    }
}

private fun androidx.compose.foundation.lazy.LazyListScope.suggestionSection(
    title: String,
    suggestions: List<IndexedSuggestion>,
    query: String,
    colors: SearchSheetColors,
    onSelectSuggestion: (IndexedSuggestion) -> Unit,
    onBrowseForMe: (String) -> Unit,
) {
    if (suggestions.isEmpty()) return

    item(key = "section-$title") {
        Text(
            text = title.uppercase(),
            style = MaterialTheme.typography.labelSmall.copy(
                fontWeight = FontWeight.Bold,
                letterSpacing = 0.8.sp,
            ),
            color = colors.secondaryForeground,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp)
                .padding(top = 12.dp, bottom = 6.dp),
        )
    }

    suggestionRows(
        suggestions = suggestions,
        query = query,
        colors = colors,
        onSelectSuggestion = onSelectSuggestion,
        onBrowseForMe = onBrowseForMe,
    )
}

private fun androidx.compose.foundation.lazy.LazyListScope.suggestionRows(
    suggestions: List<IndexedSuggestion>,
    query: String,
    colors: SearchSheetColors,
    onSelectSuggestion: (IndexedSuggestion) -> Unit,
    onBrowseForMe: (String) -> Unit,
) {
    suggestions.forEachIndexed { index, indexedSuggestion ->
        item(key = "${indexedSuggestion.sourceIndex}-${indexedSuggestion.suggestion.key}") {
            SearchSuggestionRow(
                indexedSuggestion = indexedSuggestion,
                query = query,
                colors = colors,
                onSelect = { onSelectSuggestion(indexedSuggestion) },
                onBrowseForMe = {
                    onBrowseForMe(browseQueryFor(indexedSuggestion.suggestion, query))
                },
            )
            if (index < suggestions.lastIndex) {
                HorizontalDivider(
                    modifier = Modifier.padding(start = 62.dp),
                    thickness = 1.dp,
                    color = colors.border,
                )
            }
        }
    }
}

@Composable
private fun SearchSuggestionRow(
    indexedSuggestion: IndexedSuggestion,
    query: String,
    colors: SearchSheetColors,
    onSelect: () -> Unit,
    onBrowseForMe: () -> Unit,
) {
    val suggestion = indexedSuggestion.suggestion
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 6.dp, bottom = 6.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        SearchSuggestionSelectButton(
            suggestion = suggestion,
            colors = colors,
            onSelect = onSelect,
            modifier = Modifier.weight(1f),
        )

        if (showsBrowseForMe(suggestion, query)) {
            BrowseForMePill(
                colors = colors,
                onClick = onBrowseForMe,
            )
        }
    }
}

@Composable
private fun SearchSuggestionSelectButton(
    suggestion: SuggestionViewModel,
    colors: SearchSheetColors,
    onSelect: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    Row(
        modifier = modifier
            .clip(RoundedCornerShape(14.dp))
            .background(if (isPressed) colors.rowPressed else Color.Transparent)
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                role = Role.Button,
                onClick = onSelect,
            )
            .padding(start = 8.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        LeadingSuggestionIcon(suggestion = suggestion, colors = colors)

        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text(
                text = highlightedTitle(
                    text = suggestion.title,
                    ranges = suggestion.matchRanges,
                    accent = colors.accent,
                ),
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Normal,
                color = colors.foreground,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )

            val subtitle = suggestion.subtitle.orEmpty()
            if (subtitle.isNotEmpty()) {
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.secondaryForeground,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

@Composable
private fun LeadingSuggestionIcon(
    suggestion: SuggestionViewModel,
    colors: SearchSheetColors,
) {
    val imageBitmap = remember(suggestion.icon) { suggestion.icon?.toImageBitmapOrNull() }

    Box(
        modifier = Modifier
            .size(34.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(colors.iconFill),
        contentAlignment = Alignment.Center,
    ) {
        if (imageBitmap != null) {
            Image(
                bitmap = imageBitmap,
                contentDescription = null,
                modifier = Modifier
                    .size(22.dp)
                    .clip(RoundedCornerShape(10.dp)),
            )
        } else {
            Icon(
                painter = painterResource(id = fallbackIconFor(suggestion.kind).drawableRes),
                contentDescription = null,
                modifier = Modifier.size(17.dp),
                tint = colors.mutedForeground,
            )
        }
    }
}

@Composable
private fun BrowseForMePill(colors: SearchSheetColors, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .height(44.dp)
            .clip(RoundedCornerShape(999.dp))
            .clickable(
                role = Role.Button,
                onClick = onClick,
            )
            .semantics { contentDescription = "Browse for Me" }
            .testTag("searchSheetBrowseForMeButton"),
        contentAlignment = Alignment.Center,
    ) {
        Surface(
            shape = CircleShape,
            color = Color.Black.copy(alpha = 0.44f),
            contentColor = Color.White.copy(alpha = 0.92f),
            border = BorderStroke(1.dp, colors.border),
            tonalElevation = 0.dp,
            shadowElevation = 0.dp,
        ) {
            Box(
                modifier = Modifier
                    .height(30.dp)
                    .padding(horizontal = 12.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "Browse for Me",
                    style = MaterialTheme.typography.labelMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = Color.White.copy(alpha = 0.92f),
                    maxLines = 1,
                )
            }
        }
    }
}

private fun highlightedTitle(
    text: String,
    ranges: List<List<Int>>?,
    accent: Color,
): AnnotatedString = buildAnnotatedString {
    append(text)
    ranges.orEmpty().forEach { range ->
        if (range.size != 2) return@forEach
        val start = range[0]
        val length = range[1]
        val end = start + length
        if (start < 0 || length <= 0 || end > text.length) return@forEach
        addStyle(
            style = SpanStyle(
                color = accent,
                fontWeight = FontWeight.Bold,
            ),
            start = start,
            end = end,
        )
    }
}

private fun showsBrowseForMe(suggestion: SuggestionViewModel, query: String): Boolean = when (suggestion.kind) {
    SuggestionType.Search,
    SuggestionType.AiAnswer,
    SuggestionType.Navigation,
    -> true
    SuggestionType.History -> query.isNotEmpty()
    else -> false
}

private fun browseQueryFor(suggestion: SuggestionViewModel, query: String): String {
    val aiPrefix = "ai_search:"
    if (suggestion.key.startsWith(aiPrefix)) {
        return suggestion.key.removePrefix(aiPrefix)
    }

    if (query.isNotEmpty()) {
        return query
    }

    return suggestion.executionPayload ?: suggestion.title
}

private fun fallbackIconFor(kind: SuggestionType): MahoIcon = when (kind) {
    SuggestionType.Tab -> MahoIcon.Copy
    SuggestionType.Bookmark -> MahoIcon.BookmarkBorder
    SuggestionType.History -> MahoIcon.Clock
    SuggestionType.Action -> MahoIcon.SparklesAi
    SuggestionType.Navigation -> MahoIcon.Search
    SuggestionType.AiAnswer -> MahoIcon.Search
    SuggestionType.Search -> MahoIcon.Search
    SuggestionType.Folder -> MahoIcon.Folder
    SuggestionType.ArchivedTab -> MahoIcon.Archive
    SuggestionType.ClosedTab -> MahoIcon.CircleAlert
    SuggestionType.Calculator -> MahoIcon.Search
    SuggestionType.UnitConversion -> MahoIcon.Search
}

private fun ImageData.toImageBitmapOrNull() = runCatching {
    val bytes = ByteArray(data.size) { index -> data[index].toByte() }
    BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
}.getOrNull()

private data class IndexedSuggestion(
    val sourceIndex: Int,
    val suggestion: SuggestionViewModel,
)

private data class SearchSheetColors(
    val panelBackground: Color,
    val fieldBackground: Color,
    val border: Color,
    val foreground: Color,
    val mutedForeground: Color,
    val secondaryForeground: Color,
    val accent: Color,
    val rowPressed: Color,
    val iconFill: Color,
)

private fun searchSheetColors(
    shellColors: BrowserShellColors,
    isIncognito: Boolean,
    isDarkMode: Boolean,
): SearchSheetColors {
    if (isIncognito) {
        return SearchSheetColors(
            panelBackground = Color(0xFF1A2352),
            fieldBackground = Color.White.copy(alpha = 0.08f),
            border = Color.White.copy(alpha = 0.14f),
            foreground = Color.White,
            mutedForeground = Color.White.copy(alpha = 0.70f),
            secondaryForeground = Color.White.copy(alpha = 0.56f),
            accent = Color.White,
            rowPressed = Color.White.copy(alpha = 0.06f),
            iconFill = Color.White.copy(alpha = 0.08f),
        )
    }

    if (!isDarkMode) {
        return SearchSheetColors(
            panelBackground = Color(0xFFF2F2F7),
            fieldBackground = Color.White,
            border = Color.Black.copy(alpha = 0.10f),
            foreground = Color.Black,
            mutedForeground = Color.Black.copy(alpha = 0.60f),
            secondaryForeground = Color.Black.copy(alpha = 0.42f),
            accent = Color(0xFF84B9FF),
            rowPressed = Color.Black.copy(alpha = 0.04f),
            iconFill = Color.Black.copy(alpha = 0.035f),
        )
    }

    return SearchSheetColors(
        panelBackground = shellColors.overlaySurface,
        fieldBackground = shellColors.overlaySurfaceHigh,
        border = shellColors.divider,
        foreground = shellColors.textPrimary,
        mutedForeground = shellColors.textSecondary,
        secondaryForeground = shellColors.textSecondary.copy(alpha = 0.70f),
        accent = Color(0xFF84B9FF),
        rowPressed = shellColors.overlaySurface,
        iconFill = shellColors.overlaySurface,
    )
}

private val SearchPanelMaxWidth = 640.dp
private val SearchPanelMaxHeight = 460.dp
private val SearchPanelCorner = 28.dp
private val SearchFieldHeight = 54.dp
private val SearchFieldCorner = 20.dp
