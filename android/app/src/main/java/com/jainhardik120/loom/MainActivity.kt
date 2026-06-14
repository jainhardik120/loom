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
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.jainhardik120.loom.stream.H264StreamDecoder
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
    var status by remember { mutableStateOf("Waiting for Loom stream on USB") }
    val decoder = remember {
        H264StreamDecoder(
            port = 27183,
            onStatus = { message ->
                runOnUiThread {
                    status = message
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

        Text(
            text = status,
            color = Color.White,
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(16.dp)
        )
    }
}
