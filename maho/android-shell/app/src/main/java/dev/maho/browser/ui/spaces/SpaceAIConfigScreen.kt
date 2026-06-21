@file:OptIn(ExperimentalLayoutApi::class, ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.spaces

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AssistChip
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.MahoBridge
import dev.maho.browser.SpaceAIConfig
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private data class SpaceAiOption(
    val label: String,
    val value: String?,
)

private val toneOptions = listOf(
    SpaceAiOption(label = "Default", value = null),
    SpaceAiOption(label = "Professional", value = "professional"),
    SpaceAiOption(label = "Friendly", value = "friendly"),
    SpaceAiOption(label = "Technical", value = "technical"),
    SpaceAiOption(label = "Creative", value = "creative"),
    SpaceAiOption(label = "Concise", value = "concise"),
)

private val preferredModelOptions = listOf(
    SpaceAiOption(label = "Automatic", value = null),
    SpaceAiOption(label = "GPT-4o", value = "gpt-4o"),
    SpaceAiOption(label = "GPT-4o mini", value = "gpt-4o-mini"),
    SpaceAiOption(label = "Claude Sonnet", value = "claude-sonnet"),
    SpaceAiOption(label = "Claude Haiku", value = "claude-haiku"),
    SpaceAiOption(label = "Gemini Pro", value = "gemini-pro"),
)

@Composable
fun SpaceAIConfigScreen(
    spaceId: String,
    spaceName: String,
    onBack: () -> Unit = {},
) {
    val shellColors = BrowserShellTheme.colors
    val shellMetrics = BrowserShellTheme.metrics
    val scope = rememberCoroutineScope()

    var systemPrompt by rememberSaveable(spaceId) { mutableStateOf("") }
    var tone by rememberSaveable(spaceId) { mutableStateOf<String?>(null) }
    var focusAreas by rememberSaveable(spaceId) { mutableStateOf(emptyList<String>()) }
    var preferredModel by rememberSaveable(spaceId) { mutableStateOf<String?>(null) }
    var memoryEnabled by rememberSaveable(spaceId) { mutableStateOf(true) }
    var pendingFocusArea by rememberSaveable(spaceId) { mutableStateOf("") }
    var isLoading by remember { mutableStateOf(true) }
    var isSaving by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var statusMessage by remember { mutableStateOf<String?>(null) }
    var loadedConfig by remember { mutableStateOf(SpaceAIConfig()) }

    fun normalizeString(value: String): String? = value.trim().takeIf { it.isNotEmpty() }

    fun currentConfig(): SpaceAIConfig = SpaceAIConfig(
        systemPrompt = normalizeString(systemPrompt),
        tone = tone,
        focusAreas = focusAreas.mapNotNull { normalizeString(it) }.distinct(),
        preferredModel = preferredModel,
        memoryEnabled = memoryEnabled,
    )

    val draftConfig by remember(systemPrompt, tone, focusAreas, preferredModel, memoryEnabled) {
        derivedStateOf { currentConfig() }
    }
    val isDirty by remember(isLoading, draftConfig, loadedConfig) {
        derivedStateOf { !isLoading && draftConfig != loadedConfig }
    }

    fun applyConfig(config: SpaceAIConfig) {
        systemPrompt = config.systemPrompt.orEmpty()
        tone = config.tone
        focusAreas = config.focusAreas.distinct()
        preferredModel = config.preferredModel
        memoryEnabled = config.memoryEnabled
        pendingFocusArea = ""
    }

    fun saveConfig() {
        val configToSave = draftConfig
        isSaving = true
        errorMessage = null
        statusMessage = null

        scope.launch {
            val saveSucceeded = withContext(Dispatchers.IO) {
                MahoBridge.setSpaceAIConfig(spaceId, configToSave)
            }

            if (saveSucceeded) {
                loadedConfig = configToSave
                statusMessage = "Saved AI personality for $spaceName."
            } else {
                errorMessage = "Unable to save this space personality right now."
            }

            isSaving = false
        }
    }

    LaunchedEffect(spaceId) {
        isLoading = true
        errorMessage = null
        statusMessage = null

        val config = withContext(Dispatchers.IO) {
            MahoBridge.getSpaceAIConfig(spaceId)
        } ?: SpaceAIConfig()

        loadedConfig = config
        applyConfig(config)
        isLoading = false
    }

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .testTag("aiScreenSpaceAIConfig"),
        containerColor = shellColors.overlayBackground,
        topBar = {
            TopAppBar(
                title = {
                    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Text("AI Personality")
                        Text(
                            text = spaceName,
                            style = MaterialTheme.typography.labelMedium,
                            color = shellColors.textSecondary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = shellColors.overlayBackground,
                    titleContentColor = shellColors.textPrimary,
                    navigationIconContentColor = shellColors.textPrimary,
                    actionIconContentColor = shellColors.textPrimary,
                ),
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            painter = painterResource(id = MahoIcon.NavBack.drawableRes),
                            contentDescription = "Back",
                        )
                    }
                },
                actions = {
                    TextButton(
                        onClick = ::saveConfig,
                        enabled = !isLoading && !isSaving && isDirty,
                    ) {
                        if (isSaving) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(18.dp),
                                strokeWidth = 2.dp,
                            )
                        } else {
                            Text("Save")
                        }
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(shellMetrics.sectionSpacing),
        ) {
            Surface(
                color = shellColors.overlaySurface,
                shape = RoundedCornerShape(shellMetrics.cardCorner),
                tonalElevation = 1.dp,
                shadowElevation = shellMetrics.liftedShadow,
            ) {
                Column(
                    modifier = Modifier.padding(horizontal = 18.dp, vertical = 18.dp),
                    verticalArrangement = Arrangement.spacedBy(shellMetrics.compactSpacing),
                ) {
                    Text(
                        text = "Shape how Maho thinks in this space.",
                        style = MaterialTheme.typography.titleMedium,
                        color = shellColors.textPrimary,
                    )
                    Text(
                        text = "Tune tone, prompt context, focus areas, model preference, and memory without affecting any other space.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = shellColors.textSecondary,
                    )
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        SpaceAiBadge(label = "Tone", value = selectedLabel(toneOptions, tone))
                        SpaceAiBadge(label = "Model", value = selectedLabel(preferredModelOptions, preferredModel))
                        SpaceAiBadge(label = "Memory", value = if (memoryEnabled) "On" else "Off")
                    }
                }
            }

            when {
                isLoading -> {
                    Surface(
                        color = shellColors.overlaySurface,
                        shape = RoundedCornerShape(shellMetrics.cardCorner),
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 28.dp),
                            contentAlignment = Alignment.Center,
                        ) {
                            CircularProgressIndicator()
                        }
                    }
                }

                else -> {
                    statusMessage?.let { message ->
                        Surface(
                            color = shellColors.overlaySurfaceHigh,
                            shape = RoundedCornerShape(shellMetrics.compactCorner),
                        ) {
                            Text(
                                text = message,
                                style = MaterialTheme.typography.bodyMedium,
                                color = shellColors.textPrimary,
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                            )
                        }
                    }

                    errorMessage?.let { message ->
                        Surface(
                            color = MaterialTheme.colorScheme.errorContainer,
                            shape = RoundedCornerShape(shellMetrics.compactCorner),
                        ) {
                            Text(
                                text = message,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onErrorContainer,
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                            )
                        }
                    }

                    SpaceAiSectionCard(
                        title = "System prompt",
                        description = "Set the standing instruction that frames every AI response inside this space.",
                    ) {
                        OutlinedTextField(
                            value = systemPrompt,
                            onValueChange = {
                                systemPrompt = it
                                errorMessage = null
                                statusMessage = null
                            },
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(180.dp),
                            placeholder = { Text("e.g. Keep answers rigorous, privacy-first, and biased toward implementation detail.") },
                            minLines = 6,
                            maxLines = 10,
                        )
                    }

                    SpaceAiSectionCard(
                        title = "Tone",
                        description = "Choose the voice this space should default to when Maho generates or rewrites text.",
                    ) {
                        SpaceAiSelectionPreference(
                            title = "Default tone",
                            selectedLabel = selectedLabel(toneOptions, tone),
                            options = toneOptions,
                            onSelect = {
                                tone = it
                                errorMessage = null
                                statusMessage = null
                            },
                        )
                    }

                    SpaceAiSectionCard(
                        title = "Focus areas",
                        description = "Add recurring themes so the assistant stays anchored on what matters in this space.",
                    ) {
                        if (focusAreas.isEmpty()) {
                            Text(
                                text = "No focus areas yet. Add tags like product strategy, privacy review, or bug triage.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = shellColors.textSecondary,
                            )
                            Spacer(modifier = Modifier.height(12.dp))
                        }

                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            focusAreas.forEach { area ->
                                AssistChip(
                                    onClick = {
                                        focusAreas = focusAreas.filterNot { it == area }
                                        errorMessage = null
                                        statusMessage = null
                                    },
                                    label = { Text(area) },
                                    trailingIcon = {
                                        Icon(
                                            painter = painterResource(id = MahoIcon.Close.drawableRes),
                                            contentDescription = "Remove $area",
                                        )
                                    },
                                )
                            }
                        }

                        Spacer(modifier = Modifier.height(12.dp))

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp),
                        ) {
                            OutlinedTextField(
                                value = pendingFocusArea,
                                onValueChange = { pendingFocusArea = it },
                                modifier = Modifier.weight(1f),
                                label = { Text("Add focus area") },
                                singleLine = true,
                            )
                            TextButton(
                                onClick = {
                                    val nextArea = normalizeString(pendingFocusArea) ?: return@TextButton
                                    if (nextArea !in focusAreas) {
                                        focusAreas = focusAreas + nextArea
                                    }
                                    pendingFocusArea = ""
                                    errorMessage = null
                                    statusMessage = null
                                },
                                enabled = normalizeString(pendingFocusArea) != null,
                            ) {
                                Text("Add")
                            }
                        }
                    }

                    SpaceAiSectionCard(
                        title = "Preferred model",
                        description = "Hint which model should answer here when your providers support multiple choices.",
                    ) {
                        SpaceAiSelectionPreference(
                            title = "Model hint",
                            selectedLabel = selectedLabel(preferredModelOptions, preferredModel),
                            options = preferredModelOptions,
                            onSelect = {
                                preferredModel = it
                                errorMessage = null
                                statusMessage = null
                            },
                        )
                    }

                    SpaceAiSectionCard(
                        title = "Memory",
                        description = "Keep continuity between sessions when this space benefits from accumulated context.",
                    ) {
                        ListItem(
                            headlineContent = { Text("Remember prior context") },
                            supportingContent = {
                                Text(
                                    if (memoryEnabled) {
                                        "Maho can keep context threads alive inside $spaceName."
                                    } else {
                                        "Every request in this space starts fresh."
                                    },
                                )
                            },
                            trailingContent = {
                                Switch(
                                    checked = memoryEnabled,
                                    onCheckedChange = {
                                        memoryEnabled = it
                                        errorMessage = null
                                        statusMessage = null
                                    },
                                )
                            },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun SpaceAiSectionCard(
    title: String,
    description: String,
    content: @Composable ColumnScope.() -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val shellMetrics = BrowserShellTheme.metrics

    Surface(
        color = shellColors.overlaySurface,
        shape = RoundedCornerShape(shellMetrics.cardCorner),
        tonalElevation = 1.dp,
        shadowElevation = shellMetrics.liftedShadow,
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(shellMetrics.compactSpacing),
            content = {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium,
                    color = shellColors.textPrimary,
                )
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodyMedium,
                    color = shellColors.textSecondary,
                )
                HorizontalDivider(color = shellColors.divider.copy(alpha = 0.4f))
                content()
            },
        )
    }
}

@Composable
private fun SpaceAiSelectionPreference(
    title: String,
    selectedLabel: String,
    options: List<SpaceAiOption>,
    onSelect: (String?) -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    var expanded by remember { mutableStateOf(false) }

    Box {
        ListItem(
            headlineContent = { Text(title) },
            supportingContent = { Text(selectedLabel) },
            trailingContent = {
                Icon(
                    painter = painterResource(id = MahoIcon.ExpandMore.drawableRes),
                    contentDescription = null,
                    tint = shellColors.textSecondary,
                )
            },
            modifier = Modifier
                .fillMaxWidth()
                .clickable { expanded = true },
        )

        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
        ) {
            options.forEach { option ->
                DropdownMenuItem(
                    text = { Text(option.label) },
                    onClick = {
                        onSelect(option.value)
                        expanded = false
                    },
                )
            }
        }
    }
}

@Composable
private fun SpaceAiBadge(
    label: String,
    value: String,
) {
    val shellColors = BrowserShellTheme.colors

    Surface(
        color = shellColors.overlaySurfaceHigh,
        shape = RoundedCornerShape(999.dp),
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text(
                text = label.uppercase(),
                style = MaterialTheme.typography.labelSmall,
                color = shellColors.textSecondary,
            )
            Text(
                text = value,
                style = MaterialTheme.typography.labelLarge,
                color = shellColors.textPrimary,
            )
        }
    }
}

private fun selectedLabel(
    options: List<SpaceAiOption>,
    value: String?,
): String = options.firstOrNull { it.value == value }?.label ?: options.first().label
