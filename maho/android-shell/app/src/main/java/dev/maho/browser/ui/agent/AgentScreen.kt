package dev.maho.browser.ui.agent

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Cancel
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SmartToy
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import dev.maho.browser.MahoBridge
import dev.maho.browser.support.AgentClient
import dev.maho.browser.support.AgentEvent
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

private enum class AgentTaskState {
    Idle,
    Connecting,
    Submitted,
    Working,
    Completed,
    Failed,
    Cancelled,
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AgentScreen(
    relayUrl: String = "ws://10.0.2.2:8080/ws/agent",
    initialGoal: String? = null,
    onBack: () -> Unit,
) {
    var goalInput by remember(initialGoal) { mutableStateOf(initialGoal.orEmpty()) }
    var urlSeedInput by remember { mutableStateOf("") }
    var taskState by remember { mutableStateOf(AgentTaskState.Idle) }
    var activeTaskId by remember { mutableStateOf<String?>(null) }
    var resultSummary by remember { mutableStateOf<String?>(null) }
    val eventLog = remember { mutableStateListOf<String>() }
    val scope = rememberCoroutineScope()

    val client = remember { AgentClient(relayUrl) }
    var runLocally by remember { mutableStateOf(true) }
    var activeLocalSessionPtr by remember { mutableStateOf(0L) }
    var activeLocalSessionId by remember { mutableStateOf<String?>(null) }

    DisposableEffect(client) {
        onDispose {
            client.close()
            // Single-owner free: polling Job's `invokeOnCompletion` is the sole `agentFreeSession` caller.
            if (activeLocalSessionPtr != 0L) {
                MahoBridge.agentCancel(activeLocalSessionPtr)
            }
        }
    }

    LaunchedEffect(client) {
        client.events.collectLatest { event ->
            if (!runLocally) {
                eventLog.add(formatEvent(event))
                updateTaskState(event) { newState ->
                    taskState = newState
                }
                if (event.summary != null) {
                    resultSummary = event.summary
                }
            }
        }
    }

    LaunchedEffect(client) {
        client.connected.collectLatest { isConnected ->
            if (!runLocally && isConnected && taskState == AgentTaskState.Connecting) {
                taskState = AgentTaskState.Idle
            }
        }
    }

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .testTag("aiScreenAgent"),
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            Icons.Filled.SmartToy,
                            contentDescription = null,
                            modifier = Modifier.size(22.dp),
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Web Agent")
                    }
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                ),
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
        ) {
            Spacer(modifier = Modifier.height(12.dp))

            OutlinedTextField(
                value = goalInput,
                onValueChange = { goalInput = it },
                label = { Text("Goal") },
                placeholder = { Text("e.g. Find the latest news about AI") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = false,
                maxLines = 4,
                enabled = taskState == AgentTaskState.Idle || taskState == AgentTaskState.Completed || taskState == AgentTaskState.Failed || taskState == AgentTaskState.Cancelled,
            )

            Spacer(modifier = Modifier.height(12.dp))

            OutlinedTextField(
                value = urlSeedInput,
                onValueChange = { urlSeedInput = it },
                label = { Text("Start URL (optional)") },
                placeholder = { Text("https://...") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                enabled = taskState == AgentTaskState.Idle || taskState == AgentTaskState.Completed || taskState == AgentTaskState.Failed || taskState == AgentTaskState.Cancelled,
            )

            Spacer(modifier = Modifier.height(12.dp))

            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                androidx.compose.material3.Checkbox(
                    checked = runLocally,
                    onCheckedChange = { runLocally = it },
                    enabled = taskState == AgentTaskState.Idle || taskState == AgentTaskState.Completed || taskState == AgentTaskState.Failed || taskState == AgentTaskState.Cancelled
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text("Run Agent Locally", style = MaterialTheme.typography.bodyMedium)
            }

            Spacer(modifier = Modifier.height(20.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                val canSubmit = goalInput.isNotBlank() && (taskState == AgentTaskState.Idle || taskState == AgentTaskState.Completed || taskState == AgentTaskState.Failed || taskState == AgentTaskState.Cancelled)

                Button(
                    onClick = {
                        scope.launch {
                            eventLog.clear()
                            resultSummary = null
                            if (runLocally) {
                                taskState = AgentTaskState.Working
                                val sessionId = java.util.UUID.randomUUID().toString()
                                val sessionPtr = MahoBridge.agentCreateSession(sessionId)
                                if (sessionPtr == 0L) {
                                    taskState = AgentTaskState.Failed
                                    eventLog.add("[error] Failed to create local agent session")
                                    return@launch
                                }
                                activeLocalSessionPtr = sessionPtr
                                activeLocalSessionId = sessionId
                                val sent = MahoBridge.agentSendMessage(sessionPtr, goalInput)
                                if (!sent) {
                                    MahoBridge.agentFreeSession(sessionPtr)
                                    activeLocalSessionPtr = 0L
                                    activeLocalSessionId = null
                                    taskState = AgentTaskState.Failed
                                    eventLog.add("[error] Failed to start agent task")
                                    return@launch
                                }
                                
                                val pollJob = scope.launch(Dispatchers.IO) {
                                    try {
                                        while (activeLocalSessionId == sessionId && taskState == AgentTaskState.Working) {
                                            val eventJson = MahoBridge.agentPollEvent(sessionPtr)
                                            if (eventJson != null) {
                                                withContext(Dispatchers.Main) {
                                                     val eventObj = org.json.JSONObject(eventJson as String)
                                                     val type = eventObj.optString("type")
                                                     val data = eventObj.opt("data")
                                                     
                                                     when (type) {
                                                         "token" -> {
                                                             val token = data as? String ?: ""
                                                             eventLog.add("[token] $token")
                                                         }
                                                         "complete" -> {
                                                             val payload = data as? org.json.JSONObject
                                                             val fullText = payload?.optString("full_text") ?: ""
                                                             resultSummary = fullText
                                                             taskState = AgentTaskState.Completed
                                                         }
                                                         "error" -> {
                                                             val err = data as? String ?: "Unknown error"
                                                             resultSummary = "Error: $err"
                                                             taskState = AgentTaskState.Failed
                                                         }
                                                         else -> {}
                                                     }
                                                }
                                            }
                                            kotlinx.coroutines.delay(100)
                                        }
                                    } catch (e: kotlinx.coroutines.CancellationException) {
                                        throw e
                                    } catch (e: Exception) {
                                        withContext(Dispatchers.Main) {
                                            eventLog.add("[error] Polling failed: ${e.message}")
                                            taskState = AgentTaskState.Failed
                                        }
                                    }
                                }
                                // No suspension points between `scope.launch` above and `invokeOnCompletion` below:
                                // if a future refactor inserts one, parent-scope cancellation before registration
                                // would leak the session pointer.
                                pollJob.invokeOnCompletion {
                                    if (activeLocalSessionId == sessionId) {
                                        activeLocalSessionPtr = 0L
                                        activeLocalSessionId = null
                                    }
                                    MahoBridge.agentFreeSession(sessionPtr)
                                }
                            } else {
                                taskState = AgentTaskState.Connecting
                                client.connect()
                                kotlinx.coroutines.delay(500)
                                val seed = urlSeedInput.ifBlank { null }
                                activeTaskId = client.submitTask(goalInput, seed)
                                taskState = AgentTaskState.Submitted
                            }
                        }
                    },
                    enabled = canSubmit,
                    modifier = Modifier.weight(1f),
                ) {
                    Icon(Icons.Filled.PlayArrow, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(6.dp))
                    Text("Run Agent")
                }

                AnimatedVisibility(
                    visible = taskState == AgentTaskState.Submitted || taskState == AgentTaskState.Working,
                    enter = fadeIn(),
                    exit = fadeOut(),
                ) {
                    OutlinedButton(
                        onClick = {
                            if (runLocally) {
                                if (activeLocalSessionPtr != 0L) {
                                    MahoBridge.agentCancel(activeLocalSessionPtr)
                                }
                                taskState = AgentTaskState.Cancelled
                            } else {
                                activeTaskId?.let { client.cancelTask(it) }
                                taskState = AgentTaskState.Cancelled
                            }
                        },
                        colors = ButtonDefaults.outlinedButtonColors(
                            contentColor = MaterialTheme.colorScheme.error,
                        ),
                    ) {
                        Icon(Icons.Filled.Cancel, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("Cancel")
                    }
                }
            }

            Spacer(modifier = Modifier.height(20.dp))

            AnimatedVisibility(visible = taskState == AgentTaskState.Submitted || taskState == AgentTaskState.Working) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(vertical = 8.dp),
                ) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(20.dp),
                        strokeWidth = 2.dp,
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Text(
                        text = if (taskState == AgentTaskState.Working) "Agent working..." else "Submitted...",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.primary,
                    )
                }
            }

            resultSummary?.let { summary ->
                Spacer(modifier = Modifier.height(8.dp))
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp)),
                    color = when (taskState) {
                        AgentTaskState.Completed -> MaterialTheme.colorScheme.primaryContainer
                        AgentTaskState.Failed -> MaterialTheme.colorScheme.errorContainer
                        else -> MaterialTheme.colorScheme.surfaceVariant
                    },
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = when (taskState) {
                                AgentTaskState.Completed -> "Completed"
                                AgentTaskState.Failed -> "Failed"
                                AgentTaskState.Cancelled -> "Cancelled"
                                else -> "Result"
                            },
                            style = MaterialTheme.typography.labelLarge,
                            fontWeight = FontWeight.SemiBold,
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = summary,
                            style = MaterialTheme.typography.bodyMedium,
                        )
                    }
                }
            }

            if (eventLog.isNotEmpty()) {
                Spacer(modifier = Modifier.height(20.dp))
                Text(
                    text = "Event Log",
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = FontWeight.SemiBold,
                )
                Spacer(modifier = Modifier.height(8.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
                        .padding(12.dp),
                ) {
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        eventLog.takeLast(20).forEach { entry ->
                            Text(
                                text = entry,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(32.dp))
        }
    }
}

private fun formatEvent(event: AgentEvent): String {
    val state = event.state ?: ""
    val message = event.message ?: event.summary ?: ""
    return "[$state] $message".trim()
}

private fun updateTaskState(event: AgentEvent, update: (AgentTaskState) -> Unit) {
    when (event.state) {
        "submitted" -> update(AgentTaskState.Submitted)
        "working" -> update(AgentTaskState.Working)
        "completed" -> update(AgentTaskState.Completed)
        "failed" -> update(AgentTaskState.Failed)
        "cancelled" -> update(AgentTaskState.Cancelled)
    }
}
