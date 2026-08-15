package dev.maho.browser.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.maho.browser.R
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import kotlinx.coroutines.launch

private data class OnboardingPageData(
    val title: String,
    val subtitle: String,
    val iconRes: Int,
    val iconTint: Color,
)

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun OnboardingScreen(
    onComplete: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val scope = rememberCoroutineScope()

    val pages = listOf(
        OnboardingPageData(
            title = "Built for people who\ntook browsers seriously.",
            subtitle = "Experience the next-generation desktop & mobile browsing engine with native spaces and cloud relay.",
            iconRes = R.drawable.ic_maho_star,
            iconTint = Color(0xFF818CF8),
        ),
        OnboardingPageData(
            title = "Spaces & Tabs\nSeamlessly Organized.",
            subtitle = "Keep your work, personal, and research spaces cleanly separated with live state synchronization.",
            iconRes = MahoIcon.Globe.drawableRes,
            iconTint = Color(0xFF0A84FF),
        ),
        OnboardingPageData(
            title = "Zero-Configuration\nEncrypted Sync.",
            subtitle = "Log in to effortlessly sync your tabs and bookmarks across Android, iOS, and Desktop with full privacy.",
            iconRes = MahoIcon.SyncIcon.drawableRes,
            iconTint = Color(0xFF30D158),
        ),
    )

    val pagerState = rememberPagerState(pageCount = { pages.size })

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        Color(0xFF0F172A),
                        Color(0xFF0B0F19),
                        Color(0xFF070A10),
                    )
                )
            )
            .statusBarsPadding()
            .navigationBarsPadding(),
    ) {
        Column(
            modifier = Modifier.fillMaxSize(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.weight(1f),
            ) { pageIdx ->
                val page = pages[pageIdx]
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(horizontal = 32.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    Surface(
                        shape = CircleShape,
                        color = page.iconTint.copy(alpha = 0.12f),
                        border = BorderStroke(1.dp, page.iconTint.copy(alpha = 0.28f)),
                        modifier = Modifier.size(110.dp),
                    ) {
                        Box(contentAlignment = Alignment.Center) {
                            Icon(
                                painter = painterResource(id = page.iconRes),
                                contentDescription = null,
                                tint = page.iconTint,
                                modifier = Modifier.size(60.dp),
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(40.dp))

                    Text(
                        text = page.title,
                        style = MaterialTheme.typography.headlineMedium.copy(
                            fontSize = 26.sp,
                            fontWeight = FontWeight.Bold,
                            letterSpacing = (-0.5).sp,
                            lineHeight = 32.sp,
                        ),
                        color = Color.White,
                        textAlign = TextAlign.Center,
                    )

                    Spacer(modifier = Modifier.height(14.dp))

                    Text(
                        text = page.subtitle,
                        style = MaterialTheme.typography.bodyMedium.copy(
                            fontSize = 14.5.sp,
                            lineHeight = 20.sp,
                        ),
                        color = Color.White.copy(alpha = 0.7f),
                        textAlign = TextAlign.Center,
                    )
                }
            }

            // Bottom bar with indicators and button
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 24.dp, vertical = 24.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(20.dp),
            ) {
                // Page indicator dots
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    repeat(pages.size) { idx ->
                        val isSelected = pagerState.currentPage == idx
                        Box(
                            modifier = Modifier
                                .height(8.dp)
                                .width(if (isSelected) 24.dp else 8.dp)
                                .clip(CircleShape)
                                .background(
                                    if (isSelected) Color(0xFF0A84FF) else Color.White.copy(alpha = 0.25f)
                                ),
                        )
                    }
                }

                Button(
                    onClick = {
                        if (pagerState.currentPage < pages.size - 1) {
                            scope.launch {
                                pagerState.animateScrollToPage(pagerState.currentPage + 1)
                            }
                        } else {
                            onComplete()
                        }
                    },
                    shape = RoundedCornerShape(14.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF0A84FF),
                        contentColor = Color.White,
                    ),
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(50.dp),
                ) {
                    Text(
                        text = if (pagerState.currentPage == pages.size - 1) "Get Started" else "Continue",
                        fontWeight = FontWeight.SemiBold,
                        fontSize = 16.sp,
                    )
                }
            }
        }
    }
}
