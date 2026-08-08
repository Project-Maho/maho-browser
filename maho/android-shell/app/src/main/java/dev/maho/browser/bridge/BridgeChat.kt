package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge

object BridgeChat {
    fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long =
        MahoBridge.nativeChatSessionNew(apiKey, endpoint, model, systemInstruction)

    fun chatSessionFree(sessionPtr: Long) {
        if (sessionPtr != 0L) MahoBridge.nativeChatSessionFree(sessionPtr)
    }

    fun chatSendUserTurn(sessionPtr: Long, message: String): Boolean =
        sessionPtr != 0L && MahoBridge.nativeChatSendUserTurn(sessionPtr, message)

    // Image turns are unsupported: maho-core ChatSession is text-only. Native returns false honestly.
    fun chatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean =
        sessionPtr != 0L && MahoBridge.nativeChatSendTextWithImage(sessionPtr, text, mime, data)

    fun chatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean =
        sessionPtr != 0L && MahoBridge.nativeChatSendImage(sessionPtr, mime, data)

    fun chatCancel(sessionPtr: Long): Boolean =
        sessionPtr != 0L && MahoBridge.nativeChatCancel(sessionPtr)

    fun chatSessionPollEvent(sessionPtr: Long): String? =
        if (sessionPtr == 0L) null else MahoBridge.nativeChatSessionPollEvent(sessionPtr)

    fun chatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean =
        sessionPtr != 0L && MahoBridge.nativeChatRegisterTool(sessionPtr, name, description, parametersJson)

    fun chatSendToolResult(
        sessionPtr: Long,
        toolCallId: String,
        name: String,
        output: String,
        isSuccess: Boolean,
    ): Boolean = sessionPtr != 0L &&
        MahoBridge.nativeChatSendToolResult(sessionPtr, toolCallId, name, output, isSuccess)

    fun chatAppendUserMessage(sessionPtr: Long, content: String): Boolean =
        sessionPtr != 0L && MahoBridge.nativeChatAppendUserMessage(sessionPtr, content)

    fun chatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean =
        sessionPtr != 0L && MahoBridge.nativeChatAppendAssistantMessage(sessionPtr, content, toolCallsJson)
}
