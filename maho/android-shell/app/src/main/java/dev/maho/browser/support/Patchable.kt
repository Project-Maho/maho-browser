package dev.maho.browser.support

import kotlinx.serialization.*
import kotlinx.serialization.descriptors.*
import kotlinx.serialization.encoding.*

/**
 * Represents Rust's Option<Option<T>> three-state patch semantics:
 * - Absent: field omitted from JSON (don't change)
 * - SetNull: field present as null (clear the value)
 * - Set(value): field present with value (update to value)
 */
@Serializable(with = PatchableSerializer::class)
sealed class Patchable<out T> {
    object Absent : Patchable<Nothing>()
    object SetNull : Patchable<Nothing>()
    data class Set<T>(val value: T) : Patchable<T>()
}

class PatchableSerializer<T>(private val valueSerializer: KSerializer<T>) : KSerializer<Patchable<T>> {
    override val descriptor: SerialDescriptor = valueSerializer.descriptor

    override fun serialize(encoder: Encoder, value: Patchable<T>) {
        when (value) {
            is Patchable.Absent -> {} // Should not be called — parent should skip this field
            is Patchable.SetNull -> encoder.encodeNull()
            is Patchable.Set -> encoder.encodeSerializableValue(valueSerializer, value.value)
        }
    }

    override fun deserialize(decoder: Decoder): Patchable<T> {
        return if (decoder.decodeNotNullMark()) {
            Patchable.Set(decoder.decodeSerializableValue(valueSerializer))
        } else {
            decoder.decodeNull()
            Patchable.SetNull
        }
    }
}
