package dev.maho.browser.widget

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.runtime.Composable
import androidx.compose.ui.unit.dp
import androidx.glance.GlanceId
import androidx.glance.GlanceModifier
import androidx.glance.ImageProvider
import androidx.glance.action.clickable
import androidx.glance.appwidget.action.actionStartActivity
import androidx.glance.appwidget.GlanceAppWidget
import androidx.glance.appwidget.GlanceAppWidgetReceiver
import androidx.glance.appwidget.provideContent
import androidx.glance.background
import androidx.glance.layout.Alignment
import androidx.glance.layout.Box
import androidx.glance.layout.Row
import androidx.glance.layout.fillMaxSize
import androidx.glance.layout.fillMaxWidth
import androidx.glance.layout.padding
import androidx.glance.text.Text
import androidx.glance.text.TextStyle
import dev.maho.browser.R

class SearchWidget : GlanceAppWidget() {

    override suspend fun provideGlance(context: Context, id: GlanceId) {
        provideContent {
            WidgetContent()
        }
    }

    @Composable
    private fun WidgetContent() {
        val intent = Intent(
            Intent.ACTION_VIEW,
            Uri.parse("maho://search?q=")
        ).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val action = actionStartActivity(intent)
        Box(
            modifier = GlanceModifier
                .fillMaxSize()
                .background(androidx.glance.unit.ColorProvider(R.color.widget_background))
                .clickable(onClick = action)
                .padding(horizontal = 16.dp, vertical = 12.dp),
            contentAlignment = Alignment.CenterStart,
        ) {
            Row(
                modifier = GlanceModifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                androidx.glance.Image(
                    provider = ImageProvider(R.mipmap.ic_launcher),
                    contentDescription = null,
                    modifier = GlanceModifier.padding(end = 12.dp),
                )
                Text(
                    text = "Search in Maho",
                    style = TextStyle(
                        color = androidx.glance.unit.ColorProvider(R.color.widget_text),
                    ),
                )
            }
        }
    }
}

class SearchWidgetReceiver : GlanceAppWidgetReceiver() {
    override val glanceAppWidget: GlanceAppWidget = SearchWidget()
}
