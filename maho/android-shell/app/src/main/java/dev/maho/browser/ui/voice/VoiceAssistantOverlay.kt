package dev.maho.browser.ui.voice

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import dev.maho.browser.support.VoiceSearchManager
import dev.maho.browser.support.VoiceSearchState
import dev.maho.browser.ui.theme.BrowserShellTheme
import kotlin.math.cos
import kotlin.math.sin

private const val VoiceGradientLoopDurationMillis = 9_600
private const val VoiceGradientFullTurn = 6.2831855f
private const val VoiceGradientCenterXAmplitude = 0.16f
private const val VoiceGradientCenterYAmplitude = 0.14f
private const val VoiceGradientSweepXBase = 1.02f
private const val VoiceGradientSweepYBase = 0.98f
private const val VoiceGradientSweepXAmplitude = 0.20f
private const val VoiceGradientSweepYAmplitude = 0.18f
private const val VoiceGlobalScrimAlpha = 0.16f
private const val VoiceTextVeilEdgeAlpha = 0.06f
private const val VoiceTextVeilCoreAlpha = 0.34f
private const val VoiceTextVeilLowerAlpha = 0.22f
private const val VoiceWaveformAmbientLoopDurationMillis = 1_650
private const val VoiceWaveformAmbientBase = 0.10f
private const val VoiceWaveformAmbientAmplitude = 0.17f
private const val VoiceWaveformAmbientBarPhaseOffset = 0.62f
private val VoiceMahoPurple = Color(0xFF8B5CF6)
private val VoiceSkyBlue = Color(0xFF38BDF8)
private val VoiceDeepSkyBlue = Color(0xFF0EA5E9)
private val VoiceGradientMidnight = Color(0xFF0F172A)
private val VoiceHeadlineMist = Color(0xFFE0F2FE)
private val VoiceAuroraBaseColors = listOf(
    VoiceMahoPurple,
    VoiceSkyBlue.copy(alpha = 0.98f),
    VoiceDeepSkyBlue.copy(alpha = 0.96f),
    VoiceMahoPurple.copy(alpha = 0.94f),
)
private val VoiceAuroraSkyHaloColors = listOf(
    VoiceSkyBlue.copy(alpha = 0.68f),
    Color.Transparent,
)
private val VoiceAuroraPurpleHaloColors = listOf(
    VoiceMahoPurple.copy(alpha = 0.72f),
    Color.Transparent,
)
private val VoiceAuroraRibbonColors = listOf(
    Color.Transparent,
    VoiceSkyBlue.copy(alpha = 0.38f),
    Color.Transparent,
)
private val VoiceAuroraMidnightVignetteColors = listOf(
    Color.Transparent,
    VoiceGradientMidnight.copy(alpha = 0.18f),
)
private val VoiceWaveformWeights = floatArrayOf(
    1f,
    0.94f,
    0.88f,
    0.8f,
    0.72f,
    0.64f,
    0.56f,
    0.5f,
    0.44f,
    0.38f,
    0.34f,
    0.3f,
    0.27f,
    0.24f,
)
private val WaveformWidth = 220.dp
private val WaveformHeight = 64.dp

@Composable
fun VoiceAssistantOverlay(
    manager: VoiceSearchManager,
    onDismiss: () -> Unit,
    onRetry: () -> Unit,
    modifier: Modifier = Modifier,
    isIncognito: Boolean = false,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val overlayBase = if (isIncognito) shellColors.incognitoBackground else shellColors.overlayBackground
    val overlaySurface = if (isIncognito) shellColors.incognitoSurface else shellColors.overlaySurfaceHigh
    val state = manager.state
    val liveTranscript = manager.resultText.trim()
    val isListening = state == VoiceSearchState.Listening
    val isProcessing = state == VoiceSearchState.Processing
    val headline = when {
        (isListening || isProcessing) && liveTranscript.isNotEmpty() -> liveTranscript
        state == VoiceSearchState.Error -> "Try that again?"
        else -> "Hi! How can I help?"
    }
    val statusLine = when (state) {
        VoiceSearchState.Listening -> if (liveTranscript.isEmpty()) "Listening…" else "Keep going"
        VoiceSearchState.Processing -> "Finishing transcript…"
        VoiceSearchState.Error -> manager.errorMessage.ifBlank { "Recognition failed" }
        VoiceSearchState.Idle -> "Tap the microphone and speak naturally"
    }
    val scrimInteraction = remember { MutableInteractionSource() }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false,
        ),
    ) {
        Box(
            modifier = modifier
                .fillMaxSize()
                .background(overlayBase)
                .testTag("voiceAssistantOverlay")
                .semantics {
                    stateDescription = when (state) {
                        VoiceSearchState.Listening -> "voice assistant listening"
                        VoiceSearchState.Processing -> "voice assistant processing"
                        VoiceSearchState.Error -> "voice assistant error"
                        VoiceSearchState.Idle -> "voice assistant idle"
                    }
                },
        ) {
            VoiceAssistantGlow()
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .background(overlayBase.copy(alpha = VoiceGlobalScrimAlpha)),
            )
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .clickable(
                        interactionSource = scrimInteraction,
                        indication = null,
                        onClick = onDismiss,
                    ),
            )

            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .safeDrawingPadding()
                    .padding(horizontal = metrics.sectionSpacing, vertical = metrics.sectionSpacing),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Spacer(modifier = Modifier.weight(0.96f))
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(
                            brush = Brush.verticalGradient(
                                colors = listOf(
                                    Color.Transparent,
                                    overlayBase.copy(alpha = VoiceTextVeilEdgeAlpha),
                                    overlayBase.copy(alpha = VoiceTextVeilCoreAlpha),
                                    overlayBase.copy(alpha = VoiceTextVeilLowerAlpha),
                                    Color.Transparent,
                                ),
                            ),
                            shape = RoundedCornerShape(metrics.panelCorner),
                        )
                        .padding(vertical = metrics.compactSpacing),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    GradientHeadline(
                        text = headline,
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = metrics.compactSpacing),
                    )
                    Spacer(modifier = Modifier.height(metrics.compactSpacing))
                    Text(
                        text = statusLine,
                        style = MaterialTheme.typography.bodyMedium,
                        color = shellColors.textSecondary,
                        textAlign = TextAlign.Center,
                    )
                    AnimatedVisibility(
                        visible = state == VoiceSearchState.Error,
                        enter = fadeIn(animationSpec = tween(180)),
                        exit = fadeOut(animationSpec = tween(140)),
                    ) {
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(top = metrics.sectionSpacing),
                            shape = RoundedCornerShape(metrics.cardCorner),
                            color = overlaySurface.copy(alpha = 0.72f),
                            contentColor = shellColors.textPrimary,
                            tonalElevation = 0.dp,
                            border = BorderStroke(1.dp, shellColors.divider.copy(alpha = 0.5f)),
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(metrics.compactSpacing),
                            ) {
                                Text(
                                    text = manager.errorMessage.ifBlank { "No speech detected" },
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = shellColors.textPrimary,
                                    modifier = Modifier.weight(1f),
                                )
                                TextButton(onClick = onRetry) {
                                    Text("Retry")
                                }
                            }
                        }
                    }
                }
                Spacer(modifier = Modifier.weight(1.16f))
                VoiceWaveformPill(
                    audioLevel = manager.audioLevel,
                    active = isListening || isProcessing,
                    modifier = Modifier.navigationBarsPadding(),
                )
            }
        }
    }
}

@Composable
private fun VoiceAssistantGlow(modifier: Modifier = Modifier) {
    val transition = rememberInfiniteTransition(label = "voiceGradient")
    val drift by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = VoiceGradientLoopDurationMillis, easing = LinearEasing),
            repeatMode = RepeatMode.Restart,
        ),
        label = "voiceGradientDrift",
    )

    Canvas(modifier = modifier.fillMaxSize()) {
        val phase = drift * VoiceGradientFullTurn
        fun wave(multiplier: Float, shift: Float = 0f): Float {
            return sin((phase * multiplier + shift).toDouble()).toFloat()
        }
        fun orbit(multiplier: Float, shift: Float = 0f): Float {
            return cos((phase * multiplier + shift).toDouble()).toFloat()
        }

        val radius = maxOf(size.width, size.height)
        val gradientCenter = Offset(
            x = size.width * (0.5f + VoiceGradientCenterXAmplitude * orbit(0.72f)),
            y = size.height * (0.5f + VoiceGradientCenterYAmplitude * wave(0.84f)),
        )
        val angleX = orbit(1.08f, 0.4f)
        val angleY = wave(1.08f, 0.4f)
        val sweepX = size.width * (VoiceGradientSweepXBase + VoiceGradientSweepXAmplitude * wave(0.5f))
        val sweepY = size.height * (VoiceGradientSweepYBase + VoiceGradientSweepYAmplitude * orbit(0.5f))

        drawRect(
            brush = Brush.linearGradient(
                colors = VoiceAuroraBaseColors,
                start = Offset(
                    x = gradientCenter.x - sweepX * angleX,
                    y = gradientCenter.y - sweepY * angleY,
                ),
                end = Offset(
                    x = gradientCenter.x + sweepX * angleX,
                    y = gradientCenter.y + sweepY * angleY,
                ),
            ),
        )
        drawRect(
            brush = Brush.radialGradient(
                colors = VoiceAuroraSkyHaloColors,
                center = Offset(
                    x = size.width * (0.24f + 0.30f * orbit(0.68f, 0.9f)),
                    y = size.height * (0.28f + 0.24f * wave(0.9f, 0.7f)),
                ),
                radius = radius * 0.76f,
            ),
        )
        drawRect(
            brush = Brush.radialGradient(
                colors = VoiceAuroraPurpleHaloColors,
                center = Offset(
                    x = size.width * (0.76f + 0.24f * wave(0.82f, 1.4f)),
                    y = size.height * (0.72f + 0.26f * orbit(0.74f, 0.8f)),
                ),
                radius = radius * 0.82f,
            ),
        )
        drawRect(
            brush = Brush.linearGradient(
                colors = VoiceAuroraRibbonColors,
                start = Offset(
                    x = size.width * (0.08f + 0.30f * wave(0.62f, 1.2f)),
                    y = 0f,
                ),
                end = Offset(
                    x = size.width * (0.92f + 0.24f * orbit(0.62f, 1.2f)),
                    y = size.height,
                ),
            ),
        )
        drawRect(
            brush = Brush.radialGradient(
                colors = VoiceAuroraMidnightVignetteColors,
                center = gradientCenter,
                radius = radius * 0.94f,
            ),
        )
    }
}

@Composable
private fun GradientHeadline(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(
        text = text,
        modifier = modifier,
        style = MaterialTheme.typography.displaySmall.copy(
            fontWeight = FontWeight.SemiBold,
            brush = Brush.horizontalGradient(
                colors = listOf(VoiceSkyBlue, VoiceHeadlineMist),
            ),
        ),
        textAlign = TextAlign.Center,
        maxLines = 3,
        overflow = TextOverflow.Ellipsis,
    )
}

@Composable
private fun VoiceWaveformPill(
    audioLevel: Float,
    active: Boolean,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val transition = rememberInfiniteTransition(label = "voiceWaveform")
    val pulse by transition.animateFloat(
        initialValue = 0.92f,
        targetValue = 1.08f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 740, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse,
        ),
        label = "wavePulse",
    )
    val ambientPhase by transition.animateFloat(
        initialValue = 0f,
        targetValue = VoiceGradientFullTurn,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = VoiceWaveformAmbientLoopDurationMillis, easing = LinearEasing),
            repeatMode = RepeatMode.Restart,
        ),
        label = "waveAmbientPhase",
    )

    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(999.dp),
        color = shellColors.overlaySurfaceHigh.copy(alpha = 0.66f),
        contentColor = shellColors.textPrimary,
        tonalElevation = 0.dp,
        shadowElevation = metrics.liftedShadow,
        border = BorderStroke(1.dp, shellColors.divider.copy(alpha = 0.48f)),
    ) {
        Canvas(
            modifier = Modifier
                .size(width = WaveformWidth, height = WaveformHeight)
                .padding(horizontal = 18.dp, vertical = 14.dp)
                .testTag("voiceAssistantWaveform"),
        ) {
            val barCount = 14
            val barWidth = 5.dp.toPx()
            val gap = 7.dp.toPx()
            val totalWidth = (barWidth * barCount) + (gap * (barCount - 1))
            val startX = (size.width - totalWidth) / 2f
            val centerY = size.height / 2f
            val dotRadius = 2.7.dp.toPx()
            val minBarHeight = 8.dp.toPx()
            val maxBarHeight = size.height - 4.dp.toPx()
            repeat(barCount) { index ->
                val x = startX + index * (barWidth + gap)
                val color = if (index < 4) {
                    VoiceSkyBlue
                } else {
                    shellColors.textPrimary.copy(alpha = 0.82f)
                }
                if (!active) {
                    drawCircle(
                        color = color.copy(alpha = 0.82f),
                        radius = dotRadius,
                        center = Offset(x + barWidth / 2f, centerY),
                    )
                } else {
                    val level = audioLevel.coerceIn(0f, 1f)
                    val weight = VoiceWaveformWeights[index]
                    val ambientWave = 0.5f + 0.5f * sin(
                        (ambientPhase + index * VoiceWaveformAmbientBarPhaseOffset).toDouble()
                    ).toFloat()
                    val ambientContribution = (VoiceWaveformAmbientBase + VoiceWaveformAmbientAmplitude * ambientWave) *
                        (0.72f + 0.28f * weight)
                    val audioContribution = level * weight * pulse
                    val height = (
                        minBarHeight +
                            (maxBarHeight - minBarHeight) * (ambientContribution + audioContribution)
                    )
                        .coerceIn(minBarHeight, maxBarHeight)
                    drawRoundRect(
                        color = color,
                        topLeft = Offset(x, centerY - height / 2f),
                        size = Size(barWidth, height),
                        cornerRadius = CornerRadius(barWidth / 2f, barWidth / 2f),
                    )
                }
            }
        }
    }
}
