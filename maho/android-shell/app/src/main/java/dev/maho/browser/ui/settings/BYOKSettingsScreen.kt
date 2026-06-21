package dev.maho.browser.ui.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import dev.maho.browser.MahoBridge
import dev.maho.browser.ui.icons.MahoIcon
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray

private enum class ByokProvider(
    val id: String,
    val displayName: String,
    val keyPlaceholder: String,
) {
    OpenAI(
        id = "openai",
        displayName = "OpenAI",
        keyPlaceholder = "sk-...",
    ),
    Anthropic(
        id = "anthropic",
        displayName = "Anthropic",
        keyPlaceholder = "sk-ant-...",
    );

    companion object {
        fun fromId(id: String): ByokProvider? = entries.firstOrNull { it.id == id }
    }
}

private enum class ValidationPhase {
    Idle,
    Checking,
    Valid,
    Invalid,
}

private data class ValidationStatus(
    val phase: ValidationPhase = ValidationPhase.Idle,
    val message: String? = null,
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BYOKSettingsScreen(
    onBack: () -> Unit = {},
) {
    val scope = rememberCoroutineScope()
    var savedKeys by remember { mutableStateOf<Map<ByokProvider, String>>(emptyMap()) }
    var validationStatuses by remember { mutableStateOf<Map<ByokProvider, ValidationStatus>>(emptyMap()) }
    var isLoading by remember { mutableStateOf(true) }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var showAddDialog by remember { mutableStateOf(false) }

    suspend fun refreshSavedKeys() {
        isLoading = true
        errorMessage = null

        val loadedKeys = withContext(Dispatchers.IO) { loadSavedKeys() }
        savedKeys = loadedKeys
        validationStatuses = validationStatuses.filterKeys { provider -> loadedKeys.containsKey(provider) }
        isLoading = false
    }

    LaunchedEffect(Unit) {
        refreshSavedKeys()
    }

    val availableProviders = remember(savedKeys) {
        ByokProvider.entries.filterNot { provider -> savedKeys.containsKey(provider) }
    }

    if (showAddDialog && availableProviders.isNotEmpty()) {
        var selectedProvider by remember(availableProviders) { mutableStateOf(availableProviders.first()) }
        var keyInput by remember(selectedProvider) { mutableStateOf("") }
        var addValidationStatus by remember(selectedProvider) { mutableStateOf(ValidationStatus()) }
        var saveError by remember { mutableStateOf<String?>(null) }
        var isSavingKey by remember { mutableStateOf(false) }

        AlertDialog(
            onDismissRequest = {
                if (!isSavingKey && addValidationStatus.phase != ValidationPhase.Checking) {
                    showAddDialog = false
                }
            },
            title = { Text("Add API Key") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(
                        text = "Pick a provider, validate the key, then save it locally for BYOK.",
                        style = MaterialTheme.typography.bodyMedium,
                    )

                    if (availableProviders.size == 1) {
                        Text(
                            text = "Provider: ${selectedProvider.displayName}",
                            style = MaterialTheme.typography.titleSmall,
                        )
                    } else {
                        Text(
                            text = "Provider",
                            style = MaterialTheme.typography.labelLarge,
                        )
                        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                            availableProviders.forEachIndexed { index, provider ->
                                SegmentedButton(
                                    selected = selectedProvider == provider,
                                    onClick = {
                                        selectedProvider = provider
                                        keyInput = ""
                                        addValidationStatus = ValidationStatus()
                                        saveError = null
                                    },
                                    shape = SegmentedButtonDefaults.itemShape(index = index, count = availableProviders.size),
                                ) {
                                    Text(provider.displayName)
                                }
                            }
                        }
                    }

                    OutlinedTextField(
                        value = keyInput,
                        onValueChange = {
                            keyInput = it
                            addValidationStatus = ValidationStatus()
                            saveError = null
                        },
                        label = { Text("${selectedProvider.displayName} API Key") },
                        placeholder = { Text(selectedProvider.keyPlaceholder) },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        visualTransformation = PasswordVisualTransformation(),
                    )

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        OutlinedButton(
                            onClick = {
                                val candidateKey = keyInput.trim()
                                saveError = null
                                addValidationStatus = ValidationStatus(
                                    phase = ValidationPhase.Checking,
                                    message = "Checking ${selectedProvider.displayName} key…",
                                )

                                scope.launch {
                                    val isValid = withContext(Dispatchers.IO) {
                                        MahoBridge.byokValidateKey(selectedProvider.id, candidateKey)
                                    }
                                    addValidationStatus = if (isValid) {
                                        ValidationStatus(
                                            phase = ValidationPhase.Valid,
                                            message = "Validated. Save to keep using this key.",
                                        )
                                    } else {
                                        ValidationStatus(
                                            phase = ValidationPhase.Invalid,
                                            message = "Validation failed. Double-check the provider and key.",
                                        )
                                    }
                                }
                            },
                            enabled = keyInput.trim().isNotEmpty() && !isSavingKey && addValidationStatus.phase != ValidationPhase.Checking,
                        ) {
                            Text("Validate")
                        }

                        ValidationSummary(status = addValidationStatus)
                    }

                    if (saveError != null) {
                        Text(
                            text = saveError!!,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error,
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        val candidateKey = keyInput.trim()
                        isSavingKey = true
                        saveError = null

                        scope.launch {
                            val saveSucceeded = withContext(Dispatchers.IO) {
                                MahoBridge.byokSetKey(selectedProvider.id, candidateKey)
                            }

                            if (saveSucceeded) {
                                showAddDialog = false
                                refreshSavedKeys()
                                validationStatuses = validationStatuses + (
                                    selectedProvider to ValidationStatus(
                                        phase = ValidationPhase.Valid,
                                        message = "Saved and validated.",
                                    )
                                )
                            } else {
                                isSavingKey = false
                                saveError = "Unable to save this key right now."
                            }
                        }
                    },
                    enabled = addValidationStatus.phase == ValidationPhase.Valid && !isSavingKey,
                ) {
                    Text(if (isSavingKey) "Saving…" else "Save")
                }
            },
            dismissButton = {
                TextButton(
                    onClick = { showAddDialog = false },
                    enabled = !isSavingKey && addValidationStatus.phase != ValidationPhase.Checking,
                ) {
                    Text("Cancel")
                }
            },
        )
    }

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .testTag("aiScreenBYOK"),
        topBar = {
            TopAppBar(
                title = { Text("LLM API Keys") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            painter = painterResource(id = MahoIcon.NavBack.drawableRes),
                            contentDescription = "Back",
                        )
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .verticalScroll(rememberScrollState()),
        ) {
            SectionHeader("Configured Providers")

            when {
                isLoading -> {
                    ListItem(
                        leadingContent = {
                            Box(
                                modifier = Modifier.size(24.dp),
                                contentAlignment = Alignment.Center,
                            ) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(16.dp),
                                    strokeWidth = 2.dp,
                                )
                            }
                        },
                        headlineContent = { Text("Loading saved keys") },
                        supportingContent = { Text("Checking which providers already have BYOK credentials.") },
                    )
                }

                savedKeys.isEmpty() -> {
                    ListItem(
                        leadingContent = {
                            Icon(
                                painter = painterResource(id = MahoIcon.AutofillKey.drawableRes),
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        },
                        headlineContent = { Text("No API keys saved") },
                        supportingContent = {
                            Text("Add an OpenAI or Anthropic key to use BYOK-powered AI features.")
                        },
                    )
                }

                else -> {
                    val configuredProviders = ByokProvider.entries.filter { provider -> savedKeys.containsKey(provider) }
                    configuredProviders.forEachIndexed { index, provider ->
                        val key = savedKeys.getValue(provider)
                        ProviderKeyItem(
                            provider = provider,
                            maskedKey = maskApiKey(key),
                            validationStatus = validationStatuses[provider] ?: ValidationStatus(),
                            onValidate = {
                                validationStatuses = validationStatuses + (
                                    provider to ValidationStatus(
                                        phase = ValidationPhase.Checking,
                                        message = "Checking ${provider.displayName} key…",
                                    )
                                )

                                scope.launch {
                                    val isValid = withContext(Dispatchers.IO) {
                                        MahoBridge.byokValidateKey(provider.id, key)
                                    }
                                    validationStatuses = validationStatuses + (
                                        provider to if (isValid) {
                                            ValidationStatus(
                                                phase = ValidationPhase.Valid,
                                                message = "Validated successfully.",
                                            )
                                        } else {
                                            ValidationStatus(
                                                phase = ValidationPhase.Invalid,
                                                message = "Validation failed. Delete and add a fresh key if needed.",
                                            )
                                        }
                                    )
                                }
                            },
                            onDelete = {
                                scope.launch {
                                    val deleteSucceeded = withContext(Dispatchers.IO) {
                                        MahoBridge.byokDeleteKey(provider.id)
                                    }

                                    if (deleteSucceeded) {
                                        refreshSavedKeys()
                                    } else {
                                        errorMessage = "Unable to delete the ${provider.displayName} key."
                                    }
                                }
                            },
                        )

                        if (index != configuredProviders.lastIndex) {
                            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
                        }
                    }
                }
            }

            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Add Key")
            Button(
                onClick = { showAddDialog = true },
                enabled = availableProviders.isNotEmpty(),
                modifier = Modifier.padding(horizontal = 16.dp),
            ) {
                Icon(
                    painter = painterResource(id = MahoIcon.Add.drawableRes),
                    contentDescription = null,
                )
                Spacer(modifier = Modifier.size(8.dp))
                Text("Add Key")
            }

            if (availableProviders.isEmpty()) {
                Text(
                    text = "OpenAI and Anthropic are already configured. Delete a key to replace it.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                )
            } else {
                Text(
                    text = "Available providers: ${availableProviders.joinToString { provider -> provider.displayName }}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                )
            }

            if (errorMessage != null) {
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
                ListItem(
                    leadingContent = {
                        Icon(
                            painter = painterResource(id = MahoIcon.Warning.drawableRes),
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.error,
                        )
                    },
                    headlineContent = { Text("Storage Error") },
                    supportingContent = { Text(errorMessage!!) },
                )
            }
        }
    }
}

@Composable
private fun ProviderKeyItem(
    provider: ByokProvider,
    maskedKey: String,
    validationStatus: ValidationStatus,
    onValidate: () -> Unit,
    onDelete: () -> Unit,
) {
    ListItem(
        leadingContent = {
            ValidationIcon(status = validationStatus)
        },
        headlineContent = {
            Text(provider.displayName)
        },
        supportingContent = {
            Column {
                Text(maskedKey)

                validationStatus.message?.let { message ->
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = message,
                        style = MaterialTheme.typography.bodySmall,
                        color = validationStatusColor(status = validationStatus),
                    )
                }
            }
        },
        trailingContent = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                TextButton(
                    onClick = onValidate,
                    enabled = validationStatus.phase != ValidationPhase.Checking,
                ) {
                    Text("Validate")
                }
                IconButton(
                    onClick = onDelete,
                    enabled = validationStatus.phase != ValidationPhase.Checking,
                ) {
                    Icon(
                        painter = painterResource(id = MahoIcon.Delete.drawableRes),
                        contentDescription = "Delete ${provider.displayName} key",
                        tint = MaterialTheme.colorScheme.error,
                    )
                }
            }
        },
    )
}

@Composable
private fun ValidationSummary(
    status: ValidationStatus,
) {
    if (status.phase == ValidationPhase.Idle) {
        return
    }

    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        ValidationIcon(status = status)
        status.message?.let { message ->
            Text(
                text = message,
                style = MaterialTheme.typography.bodySmall,
                color = validationStatusColor(status = status),
            )
        }
    }
}

@Composable
private fun ValidationIcon(
    status: ValidationStatus,
) {
    Box(
        modifier = Modifier.size(24.dp),
        contentAlignment = Alignment.Center,
    ) {
        when (status.phase) {
            ValidationPhase.Idle -> Icon(
                painter = painterResource(id = MahoIcon.AutofillKey.drawableRes),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            ValidationPhase.Checking -> CircularProgressIndicator(
                modifier = Modifier.size(16.dp),
                strokeWidth = 2.dp,
            )

            ValidationPhase.Valid -> Icon(
                painter = painterResource(id = MahoIcon.CheckCircle.drawableRes),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
            )

            ValidationPhase.Invalid -> Icon(
                painter = painterResource(id = MahoIcon.CircleAlert.drawableRes),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.error,
            )
        }
    }
}

@Composable
private fun validationStatusColor(
    status: ValidationStatus,
) = when (status.phase) {
    ValidationPhase.Idle -> MaterialTheme.colorScheme.onSurfaceVariant
    ValidationPhase.Checking -> MaterialTheme.colorScheme.onSurfaceVariant
    ValidationPhase.Valid -> MaterialTheme.colorScheme.primary
    ValidationPhase.Invalid -> MaterialTheme.colorScheme.error
}

private fun loadSavedKeys(): Map<ByokProvider, String> {
    val configuredProviders = parseConfiguredProviders(MahoBridge.byokGetProviders())

    return configuredProviders.mapNotNull { provider ->
        MahoBridge.byokGetKey(provider.id)
            ?.takeIf { key -> key.isNotBlank() }
            ?.let { key -> provider to key }
    }.toMap()
}

private fun parseConfiguredProviders(rawProviders: String?): List<ByokProvider> {
    if (rawProviders.isNullOrBlank()) {
        return emptyList()
    }

    return runCatching {
        val jsonArray = JSONArray(rawProviders)
        buildList {
            for (index in 0 until jsonArray.length()) {
                val provider = ByokProvider.fromId(jsonArray.optString(index))
                if (provider != null && provider !in this) {
                    add(provider)
                }
            }
        }
    }.getOrDefault(emptyList())
}

private fun maskApiKey(key: String): String {
    val visibleSuffix = key.takeLast(minOf(4, key.length))
    val hiddenLength = maxOf(8, key.length - visibleSuffix.length)
    return "•".repeat(hiddenLength) + visibleSuffix
}
