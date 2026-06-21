package dev.maho.browser.ui.chat

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.PhotoCamera
import androidx.compose.material.icons.filled.Screenshot
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import dev.maho.browser.ConversationSession
import dev.maho.browser.ConversationTurn
import dev.maho.browser.MahoBridge
import dev.maho.browser.support.VoiceSearchButton
import dev.maho.browser.support.VoiceSearchManager
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

private const val OpenAiEndpoint = "https://api.openai.com/v1"
private const val AnthropicEndpoint = "https://api.anthropic.com/v1"
private const val OpenAiModel = "gpt-4o"
private const val AnthropicModel = "claude-sonnet-4-20250514"
private const val PollIntervalMs = 50L
private const val MaxPageContextChars = 12_000
private const val MaxResumeTurns = 24
private const val MaxResumeTranscriptChars = 8_000
private const val ChatImageAttachmentLabel = "[Image attached]"
private const val ChatImageMime = "image/jpeg"

private enum class ChatRole {
    User,
    Assistant,
}

private enum class ChatProvider(
    val id: String,
    val displayName: String,
    val endpoint: String,
    val model: String,
) {
    OpenAI(
        id = "openai",
        displayName = "OpenAI",
        endpoint = OpenAiEndpoint,
        model = OpenAiModel,
    ),
    Anthropic(
        id = "anthropic",
        displayName = "Anthropic",
        endpoint = AnthropicEndpoint,
        model = AnthropicModel,
    ),
}

private data class ChatSessionConfig(
    val provider: ChatProvider,
    val apiKey: String,
    val systemInstruction: String,
)

private data class ConversationBootstrap(
    val session: ConversationSession,
    val turns: List<ConversationTurn>,
)

private data class ChatMessage(
    val id: Long,
    val role: ChatRole,
    val text: String,
    val imageMime: String? = null,
    val imageBytes: ByteArray? = null,
    val isStreaming: Boolean = false,
)

private class ChatStateHolder(
    private val scope: CoroutineScope,
    private val pageUrl: String,
    private val pageTitle: String,
    private val pageText: String,
    private val initialSessionId: String?,
) {
    val messages = mutableStateListOf<ChatMessage>()

    var inputText by mutableStateOf("")
    var sessionPtr by mutableStateOf(0L)
    var activeProvider by mutableStateOf<ChatProvider?>(null)
    var currentSessionId by mutableStateOf<String?>(initialSessionId)
    var conversationTitle by mutableStateOf<String?>(null)
    var isInitializing by mutableStateOf(false)
    var isStreaming by mutableStateOf(false)
    var missingApiKey by mutableStateOf(false)
    var errorMessage by mutableStateOf<String?>(null)

    private var pollJob: Job? = null
    private var nextMessageId = 0L

    fun initialize() {
        if (isInitializing || sessionPtr != 0L) return

        isInitializing = true
        missingApiKey = false
        errorMessage = null

        scope.launch {
            val bootstrap = withContext(Dispatchers.IO) {
                loadConversationBootstrap(initialSessionId)
            }
            currentSessionId = bootstrap?.session?.id
            conversationTitle = bootstrap?.session?.title?.takeIf { title -> title.isNotBlank() }
            hydrateMessages(bootstrap?.turns.orEmpty())

            val sessionConfig = withContext(Dispatchers.IO) {
                resolveSessionConfig(
                    pageUrl = pageUrl,
                    pageTitle = pageTitle,
                    pageText = pageText,
                    priorTurns = bootstrap?.turns.orEmpty(),
                )
            }

            if (sessionConfig == null) {
                missingApiKey = true
                activeProvider = null
                isInitializing = false
                return@launch
            }

            activeProvider = sessionConfig.provider

            val wrapperPtr = withContext(Dispatchers.IO) {
                MahoBridge.chatSessionNew(
                    sessionConfig.apiKey,
                    sessionConfig.provider.endpoint,
                    sessionConfig.provider.model,
                    sessionConfig.systemInstruction,
                )
            }

            if (wrapperPtr == 0L) {
                errorMessage = "Couldn’t start an AI chat right now."
                isInitializing = false
                return@launch
            }

            sessionPtr = wrapperPtr
            isInitializing = false
            startPolling()
        }
    }

    fun sendMessage() {
        val trimmed = inputText.trim()
        sendUserTurn(
            text = trimmed,
            imageBytes = null,
            mime = null,
        )
    }

    fun sendImageMessage(
        imageBytes: ByteArray,
        mime: String = ChatImageMime,
    ) {
        sendUserTurn(
            text = inputText.trim(),
            imageBytes = imageBytes,
            mime = mime,
        )
    }

    private fun sendUserTurn(
        text: String,
        imageBytes: ByteArray?,
        mime: String?,
    ) {
        val trimmed = text.trim()
        val wrapperPtr = sessionPtr
        val provider = activeProvider
        val existingSessionId = currentSessionId
        val derivedTitle = conversationTitle ?: deriveConversationTitle(
            pageTitle = pageTitle,
            firstUserMessage = trimmed,
            includesImage = imageBytes != null,
        )
        val pageContextUrl = pageUrl.takeIf { url -> url.isNotBlank() }
        val persistedContent = buildPersistedUserContent(
            text = trimmed,
            includesImage = imageBytes != null,
        )
        if (persistedContent.isBlank() || wrapperPtr == 0L || isInitializing || isStreaming || missingApiKey) {
            return
        }

        inputText = ""
        errorMessage = null
        messages += ChatMessage(
            id = nextMessageId(),
            role = ChatRole.User,
            text = trimmed,
            imageMime = mime,
            imageBytes = imageBytes,
        )
        ensureAssistantPlaceholder()
        isStreaming = true

        scope.launch(Dispatchers.IO) {
            val persistedSessionId = when {
                !existingSessionId.isNullOrBlank() -> existingSessionId
                provider == null -> null
                else -> {
                    val generatedSessionId = UUID.randomUUID().toString()
                    val created = MahoBridge.createConversation(
                        id = generatedSessionId,
                        title = derivedTitle,
                        spaceId = null,
                        model = provider.model,
                    )
                    if (created) generatedSessionId else null
                }
            }

            if (existingSessionId.isNullOrBlank() && !persistedSessionId.isNullOrBlank()) {
                withContext(Dispatchers.Main) {
                    currentSessionId = persistedSessionId
                    conversationTitle = derivedTitle
                }
            } else if (existingSessionId.isNullOrBlank() && provider != null) {
                withContext(Dispatchers.Main) {
                    errorMessage = "Chat started, but a saved conversation could not be created."
                }
            }

            if (!persistedSessionId.isNullOrBlank()) {
                val savedUserMessage = MahoBridge.saveConversationMessage(
                    sessionId = persistedSessionId,
                    role = "user",
                    content = persistedContent,
                    urlContext = pageContextUrl,
                )
                if (!savedUserMessage) {
                    withContext(Dispatchers.Main) {
                        errorMessage = "Message sent, but chat history could not be saved."
                    }
                }
            }

            val sent = when {
                imageBytes != null && !trimmed.isBlank() && !mime.isNullOrBlank() -> {
                    MahoBridge.chatSendTextWithImage(wrapperPtr, trimmed, mime, imageBytes)
                }

                imageBytes != null && !mime.isNullOrBlank() -> {
                    MahoBridge.chatSendImage(wrapperPtr, mime, imageBytes)
                }

                else -> MahoBridge.chatSendUserTurn(wrapperPtr, trimmed)
            }
            if (!sent) {
                withContext(Dispatchers.Main) {
                    isStreaming = false
                    removeEmptyAssistantPlaceholder()
                    errorMessage = if (imageBytes != null) {
                        "Failed to send image."
                    } else {
                        "Failed to send message."
                    }
                }
            }
        }
    }

    fun cancelStreaming() {
        val wrapperPtr = sessionPtr
        if (wrapperPtr == 0L || !isStreaming) return

        scope.launch(Dispatchers.IO) {
            MahoBridge.chatCancel(wrapperPtr)
        }
        isStreaming = false
        markAssistantMessageComplete()
    }

    fun dispose() {
        pollJob?.cancel()
        pollJob = null

        val wrapperPtr = sessionPtr
        sessionPtr = 0L
        isInitializing = false
        isStreaming = false

        if (wrapperPtr != 0L) {
            runCatching { MahoBridge.chatCancel(wrapperPtr) }
            runCatching { MahoBridge.chatSessionFree(wrapperPtr) }
        }
    }

    private fun startPolling() {
        pollJob?.cancel()
        pollJob = scope.launch(Dispatchers.IO) {
            while (isActive && sessionPtr != 0L) {
                val eventJson = MahoBridge.chatSessionPollEvent(sessionPtr)
                if (!eventJson.isNullOrBlank()) {
                    withContext(Dispatchers.Main) {
                        handleEvent(eventJson)
                    }
                }
                delay(PollIntervalMs)
            }
        }
    }

    private fun handleEvent(eventJson: String) {
        val event = runCatching { JSONObject(eventJson) }.getOrNull() ?: return
        when (event.optString("type")) {
            "token" -> {
                val token = event.optString("data")
                if (token.isNotEmpty()) {
                    appendAssistantToken(token)
                }
            }

            "complete" -> {
                val fullText = event
                    .optJSONObject("data")
                    ?.optString("full_text")
                    .orEmpty()
                val assistantText = finalizeAssistantMessage(fullText)
                isStreaming = false
                if (!assistantText.isNullOrBlank()) {
                    persistAssistantMessage(assistantText)
                }
            }

            "error" -> {
                isStreaming = false
                removeEmptyAssistantPlaceholder()
                markAssistantMessageComplete()
                errorMessage = event.optString("data").ifBlank { "The chat session failed." }
            }
        }
    }

    private fun ensureAssistantPlaceholder() {
        val lastMessage = messages.lastOrNull()
        if (lastMessage?.role == ChatRole.Assistant && lastMessage.isStreaming) {
            return
        }

        messages += ChatMessage(
            id = nextMessageId(),
            role = ChatRole.Assistant,
            text = "",
            isStreaming = true,
        )
    }

    private fun appendAssistantToken(token: String) {
        ensureAssistantPlaceholder()
        val lastIndex = messages.lastIndex
        val lastMessage = messages[lastIndex]
        messages[lastIndex] = lastMessage.copy(
            text = lastMessage.text + token,
            isStreaming = true,
        )
    }

    private fun finalizeAssistantMessage(fullText: String): String? {
        val lastMessage = messages.lastOrNull()
        return when {
            lastMessage?.role == ChatRole.Assistant -> {
                val resolvedText = fullText.ifBlank { lastMessage.text }
                if (resolvedText.isBlank()) {
                    messages.removeAt(messages.lastIndex)
                    null
                } else {
                    messages[messages.lastIndex] = lastMessage.copy(
                        text = resolvedText,
                        isStreaming = false,
                    )
                    resolvedText
                }
            }

            fullText.isNotBlank() -> {
                messages += ChatMessage(
                    id = nextMessageId(),
                    role = ChatRole.Assistant,
                    text = fullText,
                )
                fullText
            }

            else -> null
        }
    }

    private fun removeEmptyAssistantPlaceholder() {
        val lastMessage = messages.lastOrNull() ?: return
        if (lastMessage.role == ChatRole.Assistant && lastMessage.text.isBlank()) {
            messages.removeAt(messages.lastIndex)
        }
    }

    private fun markAssistantMessageComplete() {
        val lastMessage = messages.lastOrNull() ?: return
        if (lastMessage.role == ChatRole.Assistant && lastMessage.isStreaming) {
            messages[messages.lastIndex] = lastMessage.copy(isStreaming = false)
        }
    }

    private fun nextMessageId(): Long {
        nextMessageId += 1L
        return nextMessageId
    }

    private fun hydrateMessages(turns: List<ConversationTurn>) {
        messages.clear()
        nextMessageId = 0L
        turns.forEach { turn ->
            val role = parseConversationRole(turn.role) ?: return@forEach
            val text = turn.content.trim()
            if (text.isBlank()) return@forEach
            messages += ChatMessage(
                id = nextMessageId(),
                role = role,
                text = text,
            )
        }
    }

    private fun persistAssistantMessage(content: String) {
        val sessionId = currentSessionId ?: return
        scope.launch(Dispatchers.IO) {
            val savedAssistantMessage = MahoBridge.saveConversationMessage(
                sessionId = sessionId,
                role = "assistant",
                content = content,
                urlContext = null,
            )
            if (!savedAssistantMessage) {
                withContext(Dispatchers.Main) {
                    errorMessage = "Answer received, but chat history could not be saved."
                }
            }
        }
    }
}

@Composable
fun ChatScreen(
    pageUrl: String,
    pageTitle: String,
    pageText: String,
    isIncognito: Boolean,
    modifier: Modifier = Modifier,
    sessionId: String? = null,
    initialPrompt: String? = null,
    autoSendInitialPrompt: Boolean = false,
    focusInputOnLaunch: Boolean = false,
    startVoiceCaptureOnLaunch: Boolean = false,
    onOpenConversationList: () -> Unit = {},
    onSessionIdChange: (String?) -> Unit = {},
    onCaptureScreenshot: suspend () -> ByteArray? = { null },
    onDismiss: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val voiceManager = remember(context) { VoiceSearchManager(context) }
    val inputFocusRequester = remember { FocusRequester() }
    var showVisualSearch by remember { mutableStateOf(false) }
    var isCapturingScreenshot by remember { mutableStateOf(false) }
    var initialPromptHandled by remember(initialPrompt, autoSendInitialPrompt) { mutableStateOf(false) }
    val chatState = remember(scope, pageUrl, pageTitle, pageText, sessionId) {
        ChatStateHolder(
            scope = scope,
            pageUrl = pageUrl,
            pageTitle = pageTitle,
            pageText = pageText,
            initialSessionId = sessionId,
        )
    }
    val listState = rememberLazyListState()

    LaunchedEffect(chatState) {
        chatState.initialize()
    }

    DisposableEffect(chatState) {
        onDispose {
            chatState.dispose()
        }
    }

    DisposableEffect(voiceManager) {
        onDispose {
            voiceManager.reset()
        }
    }

    LaunchedEffect(chatState.currentSessionId) {
        onSessionIdChange(chatState.currentSessionId)
    }

    LaunchedEffect(
        initialPrompt,
        autoSendInitialPrompt,
        chatState.sessionPtr,
        chatState.isInitializing,
        chatState.isStreaming,
        chatState.missingApiKey,
    ) {
        val prompt = initialPrompt?.trim().orEmpty()
        if (prompt.isBlank() || initialPromptHandled) return@LaunchedEffect

        if (autoSendInitialPrompt) {
            if (
                chatState.sessionPtr != 0L &&
                !chatState.isInitializing &&
                !chatState.isStreaming &&
                !chatState.missingApiKey
            ) {
                chatState.inputText = prompt
                initialPromptHandled = true
                chatState.sendMessage()
            }
        } else {
            chatState.inputText = prompt
            initialPromptHandled = true
        }
    }

    LaunchedEffect(focusInputOnLaunch, autoSendInitialPrompt, chatState.missingApiKey) {
        if (focusInputOnLaunch && !autoSendInitialPrompt && !chatState.missingApiKey) {
            inputFocusRequester.requestFocus()
        }
    }

    val autoScrollAnchor = chatState.messages.lastOrNull()?.let { message ->
        "${chatState.messages.size}:${message.id}:${message.text.length}:${message.imageBytes?.size ?: 0}:${message.isStreaming}"
    }

    LaunchedEffect(autoScrollAnchor) {
        if (chatState.messages.isNotEmpty()) {
            listState.animateScrollToItem(chatState.messages.lastIndex)
        }
    }

    val pageContextAttached = pageUrl.isNotBlank() || pageTitle.isNotBlank() || pageText.isNotBlank()
    val panelColor = if (isIncognito) {
        shellColors.incognitoBackground.copy(alpha = 0.96f)
    } else {
        shellColors.overlaySurfaceHigh.copy(alpha = 0.96f)
    }
    val panelBorderColor = if (isIncognito) {
        shellColors.incognitoAccent.copy(alpha = 0.18f)
    } else {
        shellColors.divider.copy(alpha = 0.46f)
    }
    val listSurfaceColor = if (isIncognito) {
        shellColors.incognitoSurface.copy(alpha = 0.78f)
    } else {
        shellColors.overlaySurface.copy(alpha = 0.78f)
    }
    val actionColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent
    val bubbleBorderColor = if (isIncognito) {
        shellColors.incognitoAccent.copy(alpha = 0.18f)
    } else {
        shellColors.divider.copy(alpha = 0.38f)
    }
    val headerLabel = when {
        !chatState.currentSessionId.isNullOrBlank() && !chatState.conversationTitle.isNullOrBlank() -> chatState.conversationTitle!!
        !chatState.currentSessionId.isNullOrBlank() -> "Untitled chat"
        pageTitle.isNotBlank() -> pageTitle
        pageUrl.isNotBlank() -> displayHost(pageUrl)
        else -> "General browser assistant"
    }
    val providerLabel = chatState.activeProvider?.let { provider ->
        "${provider.displayName} · ${provider.model}"
    } ?: "Bring your own model"
    val statusLabel = when {
        isCapturingScreenshot -> "Capturing current page…"
        chatState.missingApiKey -> "Set up API key first in Settings → LLM API Keys."
        chatState.errorMessage != null -> chatState.errorMessage!!
        chatState.isInitializing -> "Connecting to $providerLabel"
        chatState.isStreaming -> "$providerLabel is responding…"
        pageContextAttached -> "Page context attached"
        else -> "Ready for a general chat"
    }
    val attachmentEnabled =
        chatState.sessionPtr != 0L &&
            !chatState.isInitializing &&
            !chatState.isStreaming &&
            !chatState.missingApiKey &&
            !isCapturingScreenshot
    val screenshotEnabled = attachmentEnabled && pageUrl.isNotBlank()
    val sendEnabled =
        chatState.inputText.trim().isNotEmpty() &&
            chatState.sessionPtr != 0L &&
            !chatState.isInitializing &&
            !chatState.isStreaming &&
            !chatState.missingApiKey

    Box(
        modifier = modifier
            .fillMaxWidth()
            .fillMaxHeight(0.84f)
            .padding(horizontal = 12.dp)
            .navigationBarsPadding()
            .imePadding()
            .testTag("aiScreenChat"),
    ) {
        Surface(
            modifier = Modifier
                .fillMaxSize()
                .border(1.dp, panelBorderColor, RoundedCornerShape(metrics.panelCorner))
                .testTag("browserChatOverlay"),
            shape = RoundedCornerShape(metrics.panelCorner),
            color = panelColor,
            tonalElevation = 0.dp,
            shadowElevation = metrics.floatingShadow,
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.Top,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        Text(
                            text = "Ask AI",
                            style = MaterialTheme.typography.headlineSmall,
                            fontWeight = FontWeight.SemiBold,
                            color = shellColors.textPrimary,
                        )
                        Text(
                            text = headerLabel,
                            style = MaterialTheme.typography.bodyMedium,
                            color = shellColors.textSecondary,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }

                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        if (chatState.isStreaming) {
                            TextButton(onClick = chatState::cancelStreaming) {
                                Text("Stop")
                            }
                        }

                        Surface(
                            shape = CircleShape,
                            color = listSurfaceColor,
                        ) {
                            IconButton(onClick = onOpenConversationList) {
                                Icon(
                                    painter = painterResource(id = MahoIcon.History.drawableRes),
                                    contentDescription = "Open saved chats",
                                    tint = shellColors.textSecondary,
                                )
                            }
                        }

                        Surface(
                            shape = CircleShape,
                            color = listSurfaceColor,
                        ) {
                            IconButton(onClick = onDismiss) {
                                Icon(
                                    painter = painterResource(id = MahoIcon.Close.drawableRes),
                                    contentDescription = "Close chat",
                                    tint = shellColors.textSecondary,
                                )
                            }
                        }
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    ChatBadge(
                        text = providerLabel,
                        containerColor = listSurfaceColor,
                        contentColor = shellColors.textPrimary,
                    )
                    ChatBadge(
                        text = if (pageContextAttached) "Page-aware" else "General",
                        containerColor = actionColor.copy(alpha = 0.14f),
                        contentColor = actionColor,
                    )
                }

                StatusBanner(
                    text = statusLabel,
                    isError = !isCapturingScreenshot && (chatState.errorMessage != null || chatState.missingApiKey),
                    isLoading = chatState.isInitializing || isCapturingScreenshot,
                    containerColor = if (!isCapturingScreenshot && (chatState.errorMessage != null || chatState.missingApiKey)) {
                        MaterialTheme.colorScheme.errorContainer
                    } else {
                        listSurfaceColor
                    },
                    contentColor = if (!isCapturingScreenshot && (chatState.errorMessage != null || chatState.missingApiKey)) {
                        MaterialTheme.colorScheme.onErrorContainer
                    } else {
                        shellColors.textSecondary
                    },
                )

                Surface(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    shape = RoundedCornerShape(metrics.cardCorner),
                    color = listSurfaceColor,
                    tonalElevation = 0.dp,
                ) {
                    if (chatState.messages.isEmpty()) {
                        EmptyChatState(
                            pageContextAttached = pageContextAttached,
                            missingApiKey = chatState.missingApiKey,
                            actionColor = actionColor,
                            textPrimary = shellColors.textPrimary,
                            textSecondary = shellColors.textSecondary,
                        )
                    } else {
                        LazyColumn(
                            state = listState,
                            modifier = Modifier
                                .fillMaxSize()
                                .padding(horizontal = 12.dp),
                            contentPadding = PaddingValues(vertical = 12.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp),
                        ) {
                            items(chatState.messages, key = { message -> message.id }) { message ->
                                ChatBubble(
                                    message = message,
                                    actionColor = actionColor,
                                    panelColor = panelColor,
                                    bubbleBorderColor = bubbleBorderColor,
                                    textPrimary = shellColors.textPrimary,
                                    textSecondary = shellColors.textSecondary,
                                )
                            }
                        }
                    }
                }

                HorizontalDivider(color = shellColors.divider.copy(alpha = 0.42f))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.Bottom,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    TextField(
                        value = chatState.inputText,
                        onValueChange = { chatState.inputText = it },
                        modifier = Modifier
                            .weight(1f)
                            .focusRequester(inputFocusRequester),
                        placeholder = {
                            Text(
                                text = if (chatState.missingApiKey) {
                                    "Set up API key first"
                                } else if (pageContextAttached) {
                                    "Ask about this page"
                                } else {
                                    "Ask anything"
                                },
                            )
                        },
                        enabled = !chatState.missingApiKey && !chatState.isInitializing,
                        minLines = 1,
                        maxLines = 4,
                        shape = RoundedCornerShape(metrics.cardCorner),
                        colors = TextFieldDefaults.colors(
                            focusedContainerColor = listSurfaceColor,
                            unfocusedContainerColor = listSurfaceColor,
                            disabledContainerColor = listSurfaceColor,
                            focusedIndicatorColor = actionColor.copy(alpha = 0.4f),
                            unfocusedIndicatorColor = bubbleBorderColor,
                            disabledIndicatorColor = bubbleBorderColor.copy(alpha = 0.6f),
                        ),
                    )

                    // TODO: Re-enable after UserContent enum (C9) lands — see hyperplan T11
                    Surface(
                        shape = CircleShape,
                        color = listSurfaceColor,
                        modifier = Modifier.alpha(0f),
                    ) {
                        IconButton(
                            onClick = {},
                            enabled = false,
                            modifier = Modifier.size(48.dp),
                        ) {
                            Icon(
                                imageVector = Icons.Default.PhotoCamera,
                                contentDescription = "Capture photo",
                                tint = shellColors.textSecondary.copy(alpha = 0.72f),
                            )
                        }
                    }

                    Surface(
                        shape = CircleShape,
                        color = listSurfaceColor,
                    ) {
                        IconButton(
                            onClick = {
                                scope.launch {
                                    chatState.errorMessage = null
                                    isCapturingScreenshot = true
                                    try {
                                        val imageBytes = onCaptureScreenshot()
                                        if (imageBytes != null) {
                                            chatState.sendImageMessage(imageBytes, ChatImageMime)
                                        } else {
                                            chatState.errorMessage = "Couldn’t capture the current page."
                                        }
                                    } finally {
                                        isCapturingScreenshot = false
                                    }
                                }
                            },
                            enabled = screenshotEnabled,
                            modifier = Modifier.size(48.dp),
                        ) {
                            Icon(
                                imageVector = Icons.Default.Screenshot,
                                contentDescription = "Capture screenshot",
                                tint = if (screenshotEnabled) {
                                    shellColors.textPrimary
                                } else {
                                    shellColors.textSecondary.copy(alpha = 0.72f)
                                },
                            )
                        }
                    }

                    VoiceSearchButton(
                        manager = voiceManager,
                        onResult = { transcribedText ->
                            chatState.inputText = transcribedText
                        },
                        startListeningOnLaunch = startVoiceCaptureOnLaunch,
                    )

                    Surface(
                        shape = CircleShape,
                        color = if (sendEnabled) actionColor else listSurfaceColor,
                    ) {
                        IconButton(
                            onClick = chatState::sendMessage,
                            enabled = sendEnabled,
                            modifier = Modifier.size(56.dp),
                        ) {
                            Icon(
                                painter = painterResource(id = MahoIcon.Send.drawableRes),
                                contentDescription = "Send message",
                                tint = if (sendEnabled) {
                                    MaterialTheme.colorScheme.onPrimary
                                } else {
                                    shellColors.textSecondary
                                },
                            )
                        }
                    }
                }
            }
        }
    }

    if (showVisualSearch) {
        VisualSearchScreen(
            isIncognito = isIncognito,
            onDismiss = { showVisualSearch = false },
            onImageReady = { mime, data ->
                chatState.sendImageMessage(data, mime)
            },
        )
    }
}

@Composable
private fun ChatBadge(
    text: String,
    containerColor: Color,
    contentColor: Color,
) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = containerColor,
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelMedium,
            color = contentColor,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
        )
    }
}

@Composable
private fun StatusBanner(
    text: String,
    isError: Boolean,
    isLoading: Boolean,
    containerColor: Color,
    contentColor: Color,
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        color = containerColor,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            when {
                isLoading -> CircularProgressIndicator(
                    modifier = Modifier.size(16.dp),
                    strokeWidth = 2.dp,
                )
                isError -> Icon(
                    painter = painterResource(id = MahoIcon.Warning.drawableRes),
                    contentDescription = null,
                    tint = contentColor,
                    modifier = Modifier.size(16.dp),
                )
                else -> Icon(
                    painter = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                    contentDescription = null,
                    tint = contentColor,
                    modifier = Modifier.size(16.dp),
                )
            }

            Text(
                text = text,
                style = MaterialTheme.typography.bodySmall,
                color = contentColor,
            )
        }
    }
}

@Composable
private fun EmptyChatState(
    pageContextAttached: Boolean,
    missingApiKey: Boolean,
    actionColor: Color,
    textPrimary: Color,
    textSecondary: Color,
) {
    Box(
        modifier = Modifier.fillMaxSize(),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Surface(
                shape = CircleShape,
                color = actionColor.copy(alpha = 0.14f),
            ) {
                Icon(
                    painter = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                    contentDescription = null,
                    tint = actionColor,
                    modifier = Modifier
                        .padding(16.dp)
                        .size(28.dp),
                )
            }

            Text(
                text = if (missingApiKey) "Bring your own model first" else "Streaming chat is ready",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = textPrimary,
                textAlign = TextAlign.Center,
            )
            Text(
                text = when {
                    missingApiKey -> "Save an OpenAI or Anthropic API key, then reopen chat to start streaming responses."
                    pageContextAttached -> "Your questions can use the current page as context while the assistant streams its answer token by token."
                    else -> "Ask a general browser question, or open chat from a page to attach live context."
                },
                style = MaterialTheme.typography.bodyMedium,
                color = textSecondary,
                textAlign = TextAlign.Center,
            )
        }
    }
}

@Composable
private fun ChatBubble(
    message: ChatMessage,
    actionColor: Color,
    panelColor: Color,
    bubbleBorderColor: Color,
    textPrimary: Color,
    textSecondary: Color,
) {
    var showExpandedImage by remember(message.id) { mutableStateOf(false) }

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = if (message.role == ChatRole.User) Arrangement.End else Arrangement.Start,
    ) {
        Surface(
            modifier = Modifier.widthIn(max = 320.dp),
            shape = RoundedCornerShape(18.dp),
            color = if (message.role == ChatRole.User) actionColor else panelColor,
            tonalElevation = 0.dp,
            shadowElevation = 0.dp,
        ) {
            Column(
                modifier = Modifier
                    .background(Color.Transparent)
                    .border(
                        width = if (message.role == ChatRole.Assistant) 1.dp else 0.dp,
                        color = if (message.role == ChatRole.Assistant) bubbleBorderColor else Color.Transparent,
                        shape = RoundedCornerShape(18.dp),
                    )
                    .padding(horizontal = 14.dp, vertical = 10.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                message.imageBytes?.let { imageBytes ->
                    ChatImageThumbnail(
                        imageBytes = imageBytes,
                        role = message.role,
                        userContentColor = MaterialTheme.colorScheme.onPrimary,
                        assistantContentColor = textPrimary,
                        onExpand = { showExpandedImage = true },
                    )
                }

                if (message.text.isNotBlank() || (message.text.isBlank() && message.isStreaming)) {
                    Text(
                        text = if (message.text.isBlank() && message.isStreaming) "…" else message.text,
                        style = MaterialTheme.typography.bodyMedium,
                        color = if (message.role == ChatRole.User) {
                            MaterialTheme.colorScheme.onPrimary
                        } else {
                            textPrimary
                        },
                    )
                }

                if (message.imageBytes != null) {
                    Text(
                        text = "Tap image to expand",
                        style = MaterialTheme.typography.labelSmall,
                        color = if (message.role == ChatRole.User) {
                            MaterialTheme.colorScheme.onPrimary.copy(alpha = 0.72f)
                        } else {
                            textSecondary.copy(alpha = 0.88f)
                        },
                    )
                }

                if (message.role == ChatRole.Assistant && message.isStreaming) {
                    Text(
                        text = "Streaming…",
                        style = MaterialTheme.typography.labelSmall,
                        color = textPrimary.copy(alpha = 0.7f),
                    )
                }
            }
        }
    }

    if (showExpandedImage && message.imageBytes != null) {
        ExpandedImageDialog(
            imageBytes = message.imageBytes,
            onDismiss = { showExpandedImage = false },
        )
    }
}

@Composable
private fun ChatImageThumbnail(
    imageBytes: ByteArray,
    role: ChatRole,
    userContentColor: Color,
    assistantContentColor: Color,
    onExpand: () -> Unit,
) {
    val previewImage = remember(imageBytes) {
        BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.size)?.asImageBitmap()
    }

    if (previewImage != null) {
        Image(
            bitmap = previewImage,
            contentDescription = "Sent image",
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(max = 240.dp)
                .clip(RoundedCornerShape(14.dp))
                .clickable(onClick = onExpand),
            contentScale = ContentScale.Crop,
        )
    } else {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onExpand),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Icon(
                imageVector = Icons.Default.Image,
                contentDescription = null,
                tint = if (role == ChatRole.User) userContentColor else assistantContentColor,
            )
            Text(
                text = "Image preview unavailable",
                style = MaterialTheme.typography.bodyMedium,
                color = if (role == ChatRole.User) userContentColor else assistantContentColor,
            )
        }
    }
}

@Composable
private fun ExpandedImageDialog(
    imageBytes: ByteArray,
    onDismiss: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val expandedImage = remember(imageBytes) {
        BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.size)?.asImageBitmap()
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false,
        ),
    ) {
        Surface(
            modifier = Modifier.fillMaxSize(),
            color = shellColors.overlayBackground.copy(alpha = 0.98f),
            tonalElevation = 0.dp,
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .navigationBarsPadding()
                    .padding(metrics.sectionSpacing),
                verticalArrangement = Arrangement.spacedBy(metrics.compactSpacing),
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End,
                ) {
                    TextButton(onClick = onDismiss) {
                        Text("Close")
                    }
                }

                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center,
                ) {
                    if (expandedImage != null) {
                        Image(
                            bitmap = expandedImage,
                            contentDescription = "Expanded image",
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Fit,
                        )
                    } else {
                        Column(
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(metrics.compactSpacing),
                        ) {
                            Icon(
                                imageVector = Icons.Default.Image,
                                contentDescription = null,
                                tint = shellColors.textSecondary,
                                modifier = Modifier.size(36.dp),
                            )
                            Text(
                                text = "Image preview unavailable",
                                style = MaterialTheme.typography.titleMedium,
                                color = shellColors.textPrimary,
                            )
                        }
                    }
                }
            }
        }
    }
}

private fun resolveSessionConfig(
    pageUrl: String,
    pageTitle: String,
    pageText: String,
    priorTurns: List<ConversationTurn>,
): ChatSessionConfig? {
    val openAiKey = MahoBridge.byokGetKey(ChatProvider.OpenAI.id)?.trim().orEmpty()
    val anthropicKey = MahoBridge.byokGetKey(ChatProvider.Anthropic.id)?.trim().orEmpty()
    val provider = when {
        openAiKey.isNotEmpty() -> ChatProvider.OpenAI to openAiKey
        anthropicKey.isNotEmpty() -> ChatProvider.Anthropic to anthropicKey
        else -> null
    } ?: return null

    return ChatSessionConfig(
        provider = provider.first,
        apiKey = provider.second,
        systemInstruction = buildSystemInstruction(
            pageUrl = pageUrl,
            pageTitle = pageTitle,
            pageText = pageText,
            priorTurns = priorTurns,
        ),
    )
}

private fun buildSystemInstruction(
    pageUrl: String,
    pageTitle: String,
    pageText: String,
    priorTurns: List<ConversationTurn>,
): String {
    val trimmedPageText = pageText.trim().take(MaxPageContextChars)
    return buildString {
        append("You are the in-browser AI assistant for Maho Browser. Keep answers practical, concise, and grounded in the user’s question.")

        if (pageUrl.isNotBlank() || pageTitle.isNotBlank() || trimmedPageText.isNotBlank()) {
            append("\n\nCurrent page context:")
            if (pageTitle.isNotBlank()) {
                append("\nTitle: ")
                append(pageTitle)
            }
            if (pageUrl.isNotBlank()) {
                append("\nURL: ")
                append(pageUrl)
            }
            if (trimmedPageText.isNotBlank()) {
                append("\nContent:\n")
                append(trimmedPageText)
            }
        }

        val priorTranscript = buildConversationTranscript(priorTurns)
        if (priorTranscript.isNotBlank()) {
            append("\n\nPrevious conversation:\n")
            append(priorTranscript)
            append("\n\nContinue naturally from this saved chat without rehashing the transcript unless the user asks for it.")
        }
    }
}

private fun loadConversationBootstrap(sessionId: String?): ConversationBootstrap? {
    val resolvedSessionId = sessionId?.takeIf { id -> id.isNotBlank() } ?: return null
    val session = MahoBridge.listConversations(limit = 500L)
        .firstOrNull { conversation -> conversation.id == resolvedSessionId }
        ?: return null
    val turns = MahoBridge.getConversationMessages(resolvedSessionId)
    return ConversationBootstrap(
        session = session,
        turns = turns,
    )
}

private fun parseConversationRole(role: String): ChatRole? {
    return when (role.lowercase()) {
        "user" -> ChatRole.User
        "assistant" -> ChatRole.Assistant
        else -> null
    }
}

private fun buildPersistedUserContent(
    text: String,
    includesImage: Boolean,
): String {
    if (!includesImage) {
        return text
    }

    return if (text.isBlank()) {
        ChatImageAttachmentLabel
    } else {
        "$text\n\n$ChatImageAttachmentLabel"
    }
}

private fun deriveConversationTitle(
    pageTitle: String,
    firstUserMessage: String,
    includesImage: Boolean,
): String? {
    val pageTitleCandidate = pageTitle.trim().takeIf { title -> title.isNotBlank() }
    if (pageTitleCandidate != null) {
        return pageTitleCandidate.take(80)
    }

    val messageCandidate = firstUserMessage.trim().replace("\n", " ")
    val textCandidate = messageCandidate.takeIf { message -> message.isNotBlank() }?.take(80)
    if (textCandidate != null) {
        return textCandidate
    }

    return if (includesImage) "Visual search" else null
}

private fun buildConversationTranscript(turns: List<ConversationTurn>): String {
    val transcriptLines = turns
        .takeLast(MaxResumeTurns)
        .mapNotNull { turn ->
            val speaker = when (turn.role.lowercase()) {
                "user" -> "User"
                "assistant" -> "Assistant"
                else -> null
            } ?: return@mapNotNull null

            val normalizedContent = turn.content
                .trim()
                .replace("\n", " ")
                .take(500)
            if (normalizedContent.isBlank()) {
                return@mapNotNull null
            }

            "$speaker: $normalizedContent"
        }
        .toMutableList()

    while (transcriptLines.joinToString("\n").length > MaxResumeTranscriptChars && transcriptLines.isNotEmpty()) {
        transcriptLines.removeAt(0)
    }

    return transcriptLines.joinToString("\n")
}

private fun displayHost(url: String): String {
    return runCatching {
        java.net.URI(url).host?.removePrefix("www.") ?: url
    }.getOrDefault(url)
}
