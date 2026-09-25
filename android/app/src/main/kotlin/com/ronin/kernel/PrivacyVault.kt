package com.ronin.kernel

import android.content.Context
import android.util.Log
import java.io.*
import java.security.KeyStore
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * Tier 4: Encrypted Privacy Vault.
 * Provides on-device AES-256-GCM file encryption and sandbox management.
 * Protects confidential documents, ID cards, and financial records.
 */
object PrivacyVault {
    private const val TAG = "Ronin_PrivacyVault"
    private const val VAULT_DIR_NAME = "vault"
    private const val HEADER_MAGIC = "RONIN_VAULT_V1"
    private const val GCM_IV_LENGTH = 12
    private const val GCM_TAG_LENGTH = 128
    private const val TRANSFORMATION = "AES/GCM/NoPadding"
    private const val KEY_ALIAS = "ronin_privacy_vault_key"
    private const val ANDROID_KEYSTORE = "AndroidKeyStore"

    data class VaultFileItem(
        val vaultFileName: String,
        val originalFileName: String,
        val originalSize: Long,
        val encryptedSize: Long,
        val lastModifiedMs: Long
    )

    data class VaultResult(
        val success: Boolean,
        val file: File? = null,
        val error: String? = null,
        val details: String = ""
    )

    fun getVaultDirectory(context: Context): File {
        val dir = File(context.filesDir, VAULT_DIR_NAME)
        if (!dir.exists()) dir.mkdirs()
        return dir
    }

    /**
     * Resolves or generates the AES-256 secret key.
     * Uses AndroidKeyStore on device with fallback to internal file-backed key for JVM/Host tests.
     */
    @Synchronized
    private fun getSecretKey(context: Context): SecretKey {
        try {
            val keyStore = KeyStore.getInstance(ANDROID_KEYSTORE)
            keyStore.load(null)
            if (keyStore.containsAlias(KEY_ALIAS)) {
                val key = keyStore.getKey(KEY_ALIAS, null) as? SecretKey
                if (key != null) return key
            }

            // Key doesn't exist yet in Keystore, generate it
            val keyGen = KeyGenerator.getInstance("AES", ANDROID_KEYSTORE)
            val spec = android.security.keystore.KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                android.security.keystore.KeyProperties.PURPOSE_ENCRYPT or android.security.keystore.KeyProperties.PURPOSE_DECRYPT
            )
                .setBlockModes(android.security.keystore.KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(android.security.keystore.KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build()
            keyGen.init(spec)
            return keyGen.generateKey()
        } catch (e: Exception) {
            // AndroidKeyStore unavailable (e.g. running in JVM unit tests or legacy environment)
            Log.w(TAG, "AndroidKeyStore unavailable (${e.message}), utilizing fallback secure master key")
            return getFallbackMasterKey(context)
        }
    }

    private fun getFallbackMasterKey(context: Context): SecretKey {
        val keyFile = File(getVaultDirectory(context), ".master.key")
        if (keyFile.exists() && keyFile.length() == 32L) {
            val bytes = keyFile.readBytes()
            return SecretKeySpec(bytes, "AES")
        }

        val random = SecureRandom()
        val keyBytes = ByteArray(32)
        random.nextBytes(keyBytes)
        keyFile.writeBytes(keyBytes)
        return SecretKeySpec(keyBytes, "AES")
    }

    /**
     * Encrypts a source file into the privacy vault sandbox.
     */
    fun encryptFile(context: Context, sourceFile: File, deleteSource: Boolean = false): VaultResult {
        if (!sourceFile.exists() || !sourceFile.isFile) {
            return VaultResult(false, error = "Source file does not exist: ${sourceFile.name}")
        }

        try {
            val vaultDir = getVaultDirectory(context)
            val vaultFileName = "enc_${System.currentTimeMillis()}_${sourceFile.name}.roninvault"
            val destVaultFile = File(vaultDir, vaultFileName)

            val secretKey = getSecretKey(context)
            val iv = ByteArray(GCM_IV_LENGTH)
            SecureRandom().nextBytes(iv)

            val cipher = Cipher.getInstance(TRANSFORMATION)
            val gcmSpec = GCMParameterSpec(GCM_TAG_LENGTH, iv)
            cipher.init(Cipher.ENCRYPT_MODE, secretKey, gcmSpec)

            val originalBytes = sourceFile.readBytes()
            val encryptedBytes = cipher.doFinal(originalBytes)

            DataOutputStream(FileOutputStream(destVaultFile)).use { out ->
                // Write Header
                out.writeUTF(HEADER_MAGIC)
                out.writeUTF(sourceFile.name)
                out.writeLong(sourceFile.length())
                out.writeInt(iv.size)
                out.write(iv)
                out.writeInt(encryptedBytes.size)
                out.write(encryptedBytes)
            }

            if (deleteSource) {
                sourceFile.delete()
            }

            Log.i(TAG, "Encrypted ${sourceFile.name} to vault: ${destVaultFile.name}")
            return VaultResult(
                success = true,
                file = destVaultFile,
                details = "Encrypted ${sourceFile.name} (${originalBytes.size} bytes) -> Vault"
            )
        } catch (e: Exception) {
            Log.e(TAG, "Encryption failed: ${e.message}", e)
            return VaultResult(false, error = "Encryption failed: ${e.message}")
        }
    }

    /**
     * Decrypts a vault file back to a target directory.
     */
    fun decryptFile(context: Context, vaultFileName: String, targetDir: File): VaultResult {
        val vaultDir = getVaultDirectory(context)
        val vaultFile = File(vaultDir, vaultFileName)
        if (!vaultFile.exists()) {
            return VaultResult(false, error = "Vault file not found: $vaultFileName")
        }

        try {
            DataInputStream(FileInputStream(vaultFile)).use { input ->
                val magic = input.readUTF()
                if (magic != HEADER_MAGIC) {
                    return VaultResult(false, error = "Corrupt vault header: $magic")
                }

                val originalName = input.readUTF()
                val originalSize = input.readLong()

                val ivSize = input.readInt()
                val iv = ByteArray(ivSize)
                input.readFully(iv)

                val encSize = input.readInt()
                val encryptedBytes = ByteArray(encSize)
                input.readFully(encryptedBytes)

                val secretKey = getSecretKey(context)
                val cipher = Cipher.getInstance(TRANSFORMATION)
                val gcmSpec = GCMParameterSpec(GCM_TAG_LENGTH, iv)
                cipher.init(Cipher.DECRYPT_MODE, secretKey, gcmSpec)

                val decryptedBytes = cipher.doFinal(encryptedBytes)

                if (!targetDir.exists()) targetDir.mkdirs()
                val targetFile = File(targetDir, originalName)
                targetFile.writeBytes(decryptedBytes)

                Log.i(TAG, "Decrypted $vaultFileName -> ${targetFile.absolutePath}")
                return VaultResult(
                    success = true,
                    file = targetFile,
                    details = "Decrypted to ${targetFile.name} ($originalSize bytes)"
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "Decryption failed: ${e.message}", e)
            return VaultResult(false, error = "Decryption failed: ${e.message}")
        }
    }

    /**
     * Lists all files currently stored in the encrypted vault.
     */
    fun listVault(context: Context): List<VaultFileItem> {
        val vaultDir = getVaultDirectory(context)
        val files = vaultDir.listFiles { f -> f.isFile && f.name.endsWith(".roninvault") } ?: return emptyList()

        val results = mutableListOf<VaultFileItem>()
        for (f in files) {
            try {
                DataInputStream(FileInputStream(f)).use { input ->
                    val magic = input.readUTF()
                    if (magic == HEADER_MAGIC) {
                        val originalName = input.readUTF()
                        val originalSize = input.readLong()
                        results.add(
                            VaultFileItem(
                                vaultFileName = f.name,
                                originalFileName = originalName,
                                originalSize = originalSize,
                                encryptedSize = f.length(),
                                lastModifiedMs = f.lastModified()
                            )
                        )
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Failed reading vault item ${f.name}: ${e.message}")
            }
        }
        return results
    }

    /**
     * Securely deletes a file from the vault.
     */
    fun deleteVaultFile(context: Context, vaultFileName: String): Boolean {
        val vaultDir = getVaultDirectory(context)
        val file = File(vaultDir, vaultFileName)
        if (!file.exists()) return false

        try {
            // Overwrite before deletion
            val len = file.length()
            val zeros = ByteArray(len.coerceAtMost(4096).toInt())
            FileOutputStream(file).use { out ->
                var written = 0L
                while (written < len) {
                    val toWrite = (len - written).coerceAtMost(zeros.size.toLong()).toInt()
                    out.write(zeros, 0, toWrite)
                    written += toWrite
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Zeroing failed before deletion: ${e.message}")
        }
        return file.delete()
    }
}
