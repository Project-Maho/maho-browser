package dev.maho.browser.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.maho.browser.ui.theme.BrowserShellTheme

@Composable
fun MahoGroupedSection(
    title: String? = null,
    footer: String? = null,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val shellMetrics = BrowserShellTheme.metrics

    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 6.dp),
    ) {
        if (title != null) {
            Text(
                text = title.uppercase(),
                style = MaterialTheme.typography.labelSmall.copy(
                    fontSize = 11.5.sp,
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 0.8.sp,
                ),
                color = shellColors.textSecondary,
                modifier = Modifier.padding(start = 12.dp, bottom = 6.dp, top = 10.dp),
            )
        }

        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(shellMetrics.groupedCorner),
            color = shellColors.cardBackground,
            border = BorderStroke(0.5.dp, shellColors.cardBorder),
        ) {
            Column(modifier = Modifier.fillMaxWidth()) {
                content()
            }
        }

        if (footer != null) {
            Text(
                text = footer,
                style = MaterialTheme.typography.bodySmall.copy(fontSize = 12.sp),
                color = shellColors.textSecondary,
                modifier = Modifier.padding(start = 12.dp, top = 6.dp, end = 12.dp),
            )
        }
    }
}

@Composable
fun MahoGroupedRow(
    title: String,
    subtitle: String? = null,
    icon: Painter? = null,
    iconTint: Color = BrowserShellTheme.colors.accent,
    showChevron: Boolean = false,
    onClick: (() -> Unit)? = null,
    trailingContent: (@Composable RowScope.() -> Unit)? = null,
) {
    val shellColors = BrowserShellTheme.colors

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(
                if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier
            )
            .padding(horizontal = 16.dp, vertical = 13.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.weight(1f, fill = false),
        ) {
            if (icon != null) {
                Surface(
                    shape = RoundedCornerShape(7.dp),
                    color = iconTint.copy(alpha = 0.16f),
                    modifier = Modifier.size(30.dp),
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            painter = icon,
                            contentDescription = null,
                            tint = iconTint,
                            modifier = Modifier.size(17.dp),
                        )
                    }
                }
                Spacer(modifier = Modifier.width(14.dp))
            }

            Column {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Medium,
                    ),
                    color = shellColors.textPrimary,
                )
                if (subtitle != null) {
                    Text(
                        text = subtitle,
                        style = MaterialTheme.typography.bodySmall.copy(fontSize = 12.5.sp),
                        color = shellColors.textSecondary,
                        modifier = Modifier.padding(top = 1.dp),
                    )
                }
            }
        }

        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            trailingContent?.invoke(this)
            if (showChevron) {
                Text(
                    text = "›",
                    color = shellColors.textTertiary,
                    fontSize = 20.sp,
                    fontWeight = FontWeight.Light,
                )
            }
        }
    }
}

@Composable
fun MahoGroupedToggle(
    title: String,
    subtitle: String? = null,
    icon: Painter? = null,
    iconTint: Color = BrowserShellTheme.colors.accent,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    val shellColors = BrowserShellTheme.colors

    MahoGroupedRow(
        title = title,
        subtitle = subtitle,
        icon = icon,
        iconTint = iconTint,
        trailingContent = {
            Switch(
                checked = checked,
                onCheckedChange = onCheckedChange,
                colors = SwitchDefaults.colors(
                    checkedThumbColor = Color.White,
                    checkedTrackColor = shellColors.accent,
                    uncheckedThumbColor = Color.White,
                    uncheckedTrackColor = shellColors.fieldBackground,
                ),
            )
        },
    )
}

@Composable
fun MahoTextField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String = "",
    label: String? = null,
    modifier: Modifier = Modifier,
    visualTransformation: VisualTransformation = VisualTransformation.None,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default,
    keyboardActions: KeyboardActions = KeyboardActions.Default,
    singleLine: Boolean = true,
) {
    val shellColors = BrowserShellTheme.colors
    val shellMetrics = BrowserShellTheme.metrics

    Column(modifier = modifier.fillMaxWidth()) {
        if (label != null) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall.copy(
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium,
                ),
                color = shellColors.textSecondary,
                modifier = Modifier.padding(start = 4.dp, bottom = 4.dp),
            )
        }

        BasicTextField(
            value = value,
            onValueChange = onValueChange,
            singleLine = singleLine,
            visualTransformation = visualTransformation,
            keyboardOptions = keyboardOptions,
            keyboardActions = keyboardActions,
            textStyle = TextStyle(
                color = shellColors.textPrimary,
                fontSize = 15.sp,
                fontWeight = FontWeight.Normal,
            ),
            cursorBrush = SolidColor(shellColors.accent),
            decorationBox = { innerTextField ->
                Surface(
                    shape = RoundedCornerShape(shellMetrics.fieldCorner),
                    color = shellColors.fieldBackground,
                    border = BorderStroke(0.5.dp, shellColors.fieldBorder),
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(44.dp),
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 14.dp),
                        contentAlignment = Alignment.CenterStart,
                    ) {
                        if (value.isEmpty()) {
                            Text(
                                text = placeholder,
                                style = TextStyle(
                                    color = shellColors.textSecondary.copy(alpha = 0.7f),
                                    fontSize = 15.sp,
                                ),
                            )
                        }
                        innerTextField()
                    }
                }
            },
        )
    }
}

@Composable
fun MahoRowDivider() {
    val shellColors = BrowserShellTheme.colors
    HorizontalDivider(
        color = shellColors.divider,
        thickness = 0.5.dp,
        modifier = Modifier.padding(start = 16.dp),
    )
}
