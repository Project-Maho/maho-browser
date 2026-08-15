package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge

object BridgeChat {
    fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long =
        MahoBridge.invokeChatSessionNew(apiKey, endpoint, model, systemInstruction)

    fun chatSessionFree(sessionPtr: Long) {
        if (sessionPtr != 0L) MahoBridge.invokeChatSessionFree(sessionPtr)
    }

    fun chatSendUserTurn(sessionPtr: Long, message: String): Boolean =
        sessionPtr != 0L && MahoBridge.invokeChatSendUserTurn(sessionPtr, message)

    // Image turns are unsupported: maho-core ChatSession is text-only. Native returns false honestly.
    fun chatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean =
        sessionPtr != 0L && MahoBridge.invokeChatSendTextWithImage(sessionPtr, text, mime, data)

    fun chatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean =
        sessionPtr != 0L && MahoBridge.invokeChatSendImage(sessionPtr, mime, data)

    fun chatCancel(sessionPtr: Long): Boolean =
        sessionPtr != 0L && MahoBridge.invokeChatCancel(sessionPtr)

    fun chatSessionPollEvent(sessionPtr: Long): String? =
        if (sessionPtr == 0L) null else MahoBridge.invokeChatSessionPollEvent(sessionPtr)

    fun chatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean =
        sessionPtr != 0L && MahoBridge.invokeChatRegisterTool(sessionPtr, name, description, parametersJson)

    fun chatSendToolResult(
        sessionPtr: Long,
        toolCallId: String,
        name: String,
        output: String,
        trigger: Boolean,
    ): Boolean = sessionPtr != 0L &&
        MahoBridge.invokeChatSendToolResult(sessionPtr, toolCallId, name, output, trigger)

    fun chatAppendUserMessage(sessionPtr: Long, content: String): Boolean =
        sessionPtr != 0L && MahoBridge.invokeChatAppendUserMessage(sessionPtr, content)

    fun chatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean =
        sessionPtr != 0L && MahoBridge.invokeChatAppendAssistantMessage(sessionPtr, content, toolCallsJson)
}
