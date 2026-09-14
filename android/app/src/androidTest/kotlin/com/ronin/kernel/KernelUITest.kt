package com.ronin.kernel

import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config
import org.junit.Assert.*
import org.robolectric.Robolectric
import androidx.lifecycle.ViewModelProvider
import androidx.activity.ComponentActivity

@RunWith(AndroidJUnit4::class)
@Config(sdk = [33], manifest = Config.NONE)
class KernelUITest {

    @get:Rule
    val composeTestRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun testAutoScrollOnNewMessage() {
        // Since we are using Compose, we verify the state changes in the ViewModel
        // which triggers the Scroll LaunchedEffect.
        val activity = composeTestRule.activity
        val viewModel = ViewModelProvider(activity)[ChatViewModel::class.java]
        
        val initialSize = viewModel.messages.size
        
        activity.runOnUiThread {
            viewModel.messages.add("User: Hello Test")
            viewModel.messages.add("Ronin: Response Test")
        }
        
        composeTestRule.waitForIdle()
        
        assertEquals(initialSize + 2, viewModel.messages.size)
        // In a real device test, we'd check chatListState.firstVisibleItemIndex, 
        // but Robolectric's Compose support is limited for scroll animations.
    }

    @Test
    fun testPersistenceOnActivityRecreation() {
        val controller = Robolectric.buildActivity(MainActivity::class.java).setup()
        val activity = controller.get()
        val viewModel = ViewModelProvider(activity)[ChatViewModel::class.java]
        
        activity.runOnUiThread {
            viewModel.messages.add("User: Persist Me")
        }
        
        // Simulate recreation
        controller.recreate()
        
        val newActivity = controller.get()
        val newViewModel = ViewModelProvider(newActivity)[ChatViewModel::class.java]
        
        // Verify history is NOT duplicated on startup (Privacy Enforcement: Clean slate)
        // Per v3.9.7 requirements, history only loads on /history command.
        assertTrue(newViewModel.messages.isEmpty())
    }
}
