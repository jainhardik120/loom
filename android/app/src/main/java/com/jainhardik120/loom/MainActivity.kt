package com.jainhardik120.loom

import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.jainhardik120.loom.stream.H264StreamDecoder
import com.jainhardik120.loom.stream.StreamStats
import com.jainhardik120.loom.ui.theme.LoomTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        enableEdgeToEdge()
        setContent {
            LoomTheme {
                StreamScreen { update ->
                    runOnUiThread(update)
                }
            }
        }
    }
}

@Composable
private fun StreamScreen(runOnUiThread: (() -> Unit) -> Unit) {
    var stats by remember { mutableStateOf(StreamStats(status = "Waiting for Loom stream on USB")) }
    val decoder = remember {
        H264StreamDecoder(
            port = 27183,
            onStats = { nextStats ->
                runOnUiThread {
                    stats = nextStats
                }
            }
        )
    }

    DisposableEffect(decoder) {
        onDispose {
            decoder.stop()
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
    ) {
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { context ->
                SurfaceView(context).apply {
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) {
                            decoder.start(holder.surface)
                        }

                        override fun surfaceChanged(
                            holder: SurfaceHolder,
                            format: Int,
                            width: Int,
                            height: Int
                        ) = Unit

                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                            decoder.stop()
                        }
                    })
                    if (holder.surface.isValid) {
                        decoder.start(holder.surface)
                    }
                }
            }
        )

        DebugStatsPanel(
            stats = stats,
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(16.dp)
        )
    }
}

@Composable
private fun DebugStatsPanel(stats: StreamStats, modifier: Modifier = Modifier) {
    val resolution = if (stats.width > 0 && stats.height > 0) {
        "${stats.width}x${stats.height}"
    } else {
        "not negotiated"
    }

    Column(
        modifier = modifier
            .background(Color(0xAA000000), RoundedCornerShape(6.dp))
            .padding(horizontal = 10.dp, vertical = 8.dp)
    ) {
        DebugLine("status", stats.status)
        DebugLine("fps", "%.1f".format(stats.fps))
        DebugLine("resolution", resolution)
        DebugLine("bitrate", "%.0f kbps".format(stats.bitrateKbps))
        DebugLine("rendered", stats.renderedFrames.toString())
        DebugLine("queued samples", stats.queuedSamples.toString())
        DebugLine("dropped nals", stats.droppedNals.toString())
        DebugLine("port", stats.port.toString())
    }
}

@Composable
private fun DebugLine(label: String, value: String) {
    Text(
        text = "$label: $value",
        color = Color.White,
        style = MaterialTheme.typography.bodySmall,
        fontFamily = FontFamily.Monospace,
        fontSize = 12.sp,
        lineHeight = 15.sp
    )
}
