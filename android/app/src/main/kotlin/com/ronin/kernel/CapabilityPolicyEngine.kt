package com.ronin.kernel

import android.content.Context
import android.content.pm.PackageManager
import android.util.Log

/**
 * Phase 5: Capability Policy Engine
 * Centralizes sensitive action rules, permission requirements, HITL enforcement,
 * auditing, and failure telemetry reporting for agentic capabilities.
 */
object CapabilityPolicyEngine {
    private const val TAG = "CapabilityPolicyEngine"

    enum class RiskLevel(val value: Int) {
        LOW(0), MEDIUM(1), HIGH(2), EXTREME(3)
    }

    data class Policy(
        val capabilityId: String,
        val requiredPermissions: List<String>,
        val requiresHITL: Boolean,
        val requiresAudit: Boolean,
        val riskLevel: RiskLevel,
        val allowOffline: Boolean,
        val allowCloud: Boolean
    )

    private val policies = mapOf(
        "SMS" to Policy(
            "SMS",
            listOf(android.Manifest.permission.SEND_SMS),
            requiresHITL = true,
            requiresAudit = true,
            RiskLevel.HIGH,
            allowOffline = true,
            allowCloud = false
        ),
        "CONTACTS" to Policy(
            "CONTACTS",
            listOf(android.Manifest.permission.READ_CONTACTS),
            requiresHITL = false,
            requiresAudit = true,
            RiskLevel.MEDIUM,
            allowOffline = true,
            allowCloud = true
        ),
        "CALENDAR" to Policy(
            "CALENDAR",
            listOf(android.Manifest.permission.READ_CALENDAR, android.Manifest.permission.WRITE_CALENDAR),
            requiresHITL = true,
            requiresAudit = true,
            RiskLevel.MEDIUM,
            allowOffline = true,
            allowCloud = true
        ),
        "VAULT" to Policy(
            "VAULT",
            emptyList(),
            requiresHITL = true,
            requiresAudit = true,
            RiskLevel.EXTREME,
            allowOffline = true,
            allowCloud = false
        ),
        "FILES" to Policy(
            "FILES",
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                emptyList()
            } else {
                listOf(android.Manifest.permission.READ_EXTERNAL_STORAGE)
            },
            requiresHITL = false,
            requiresAudit = true,
            RiskLevel.MEDIUM,
            allowOffline = true,
            allowCloud = true
        ),
        "LOCATION" to Policy(
            "LOCATION",
            listOf(android.Manifest.permission.ACCESS_FINE_LOCATION),
            requiresHITL = false,
            requiresAudit = true,
            RiskLevel.MEDIUM,
            allowOffline = true,
            allowCloud = true
        ),
        "MAP" to Policy(
            "MAP",
            listOf(android.Manifest.permission.ACCESS_FINE_LOCATION),
            requiresHITL = true,
            requiresAudit = true,
            RiskLevel.LOW,
            allowOffline = true,
            allowCloud = true
        ),
        "SENSOR" to Policy(
            "SENSOR",
            emptyList(),
            requiresHITL = false,
            requiresAudit = true,
            RiskLevel.LOW,
            allowOffline = true,
            allowCloud = true
        ),
        "MAIL" to Policy(
            "MAIL",
            emptyList(),
            requiresHITL = true,
            requiresAudit = true,
            RiskLevel.MEDIUM,
            allowOffline = true,
            allowCloud = true
        )
    )

    fun getPolicy(capabilityId: String): Policy? = policies[capabilityId.uppercase()]

    fun evaluate(
        context: Context,
        nativeEngine: NativeEngine,
        capabilityId: String,
        params: Map<String, String>,
        isOffline: Boolean,
        isCloudOnly: Boolean
    ): EvaluationResult {
        val policy = getPolicy(capabilityId) ?: return EvaluationResult.Allowed

        // 1. Offline Mode restriction
        if (isOffline && !policy.allowOffline) {
            val reason = "Offline Mode blocks capability: $capabilityId"
            auditAndReportFailure(nativeEngine, policy, params, reason)
            return EvaluationResult.Denied(reason)
        }

        // 2. Cloud Only Mode restriction
        if (isCloudOnly && !policy.allowCloud) {
            val reason = "Cloud-Only Mode blocks local capability: $capabilityId"
            auditAndReportFailure(nativeEngine, policy, params, reason)
            return EvaluationResult.Denied(reason)
        }

        // 3. Android runtime permissions check
        for (permission in policy.requiredPermissions) {
            if (context.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                val reason = "Android Permission Denied: $permission"
                auditAndReportFailure(nativeEngine, policy, params, reason)
                return EvaluationResult.Denied(reason)
            }
        }

        // 4. Sensitive fact access check within MEMORY / DATABASE operations
        if (capabilityId.uppercase() == "MEMORY" || capabilityId.uppercase() == "DATABASE") {
            if (isSensitiveFact(params)) {
                Log.i(TAG, "Access to sensitive facts detected in memory parameters: $params")
                if (policy.requiresAudit) {
                    nativeEngine.storeAuditLog("SENSITIVE_FACT_ACCESS", "Accessed/modified sensitive fact: $params")
                }
            }
        }

        // Audit allowed action if required
        if (policy.requiresAudit) {
            nativeEngine.storeAuditLog("CAPABILITY_ALLOWED", "Allowed execution of $capabilityId")
        }

        return EvaluationResult.Allowed
    }

    fun isSensitiveFact(params: Map<String, String>): Boolean {
        val attribute = (params["attribute"] ?: "").lowercase()
        val value = (params["value"] ?: "").lowercase()
        val entity = (params["entity"] ?: "").lowercase()

        val isSensitiveAttr = attribute.contains("birthday") || attribute.contains("မွေးနေ့") ||
                attribute.contains("key") || attribute.contains("api") || attribute.contains("token") ||
                attribute.contains("password") || attribute.contains("secret") ||
                attribute.contains("medicine") || attribute.contains("ဆေး")

        val isSensitiveVal = value.contains("api_key") || value.contains("password") || value.contains("secret")

        return isSensitiveAttr || isSensitiveVal
    }

    fun auditAndReportFailure(
        nativeEngine: NativeEngine,
        policy: Policy,
        params: Map<String, String>,
        reason: String
    ) {
        val execId = params["exec_id"] ?: ""
        val nodeId = params["node_id"]?.toIntOrNull() ?: 1

        Log.e(TAG, "Capability execution policy denied: $reason")
        
        // 1. Audit log
        nativeEngine.storeAuditLog("POLICY_DENIED", "Capability: ${policy.capabilityId}, Reason: $reason")

        // 2. Failure telemetry (reportSemanticFailure)
        nativeEngine.reportSemanticFailure(
            execId,
            nodeId.toString(),
            403, // Policy violation / Access denied code
            "Policy Denied: $reason"
        )

        // 3. Graph confidence outcome update
        nativeEngine.reportOutcome(
            sourceId = 0,
            targetId = nodeId,
            success = false,
            risk = translateRiskLevel(policy.riskLevel)
        )
    }

    private fun translateRiskLevel(level: RiskLevel): NativeEngine.RiskLevel {
        return when (level) {
            RiskLevel.LOW -> NativeEngine.RiskLevel.LOW
            RiskLevel.MEDIUM -> NativeEngine.RiskLevel.MEDIUM
            RiskLevel.HIGH -> NativeEngine.RiskLevel.HIGH
            RiskLevel.EXTREME -> NativeEngine.RiskLevel.EXTREME
        }
    }

    sealed class EvaluationResult {
        object Allowed : EvaluationResult()
        data class Denied(val reason: String) : EvaluationResult()
    }
}
