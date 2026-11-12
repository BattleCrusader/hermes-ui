// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/session_usage.h"

#include <algorithm>

namespace hermes::ui {
namespace {

bool Has(const QJsonObject &nested, const QJsonObject &payload,
         const char *key) {
  return nested.contains(QLatin1String(key)) ||
         payload.contains(QLatin1String(key));
}

double Read(const QJsonObject &nested, const QJsonObject &payload,
            const char *key, const char *fallback = nullptr) {
  if (nested.contains(QLatin1String(key))) {
    return nested.value(QLatin1String(key)).toDouble();
  }
  if (payload.contains(QLatin1String(key))) {
    return payload.value(QLatin1String(key)).toDouble();
  }
  if (fallback != nullptr) {
    if (nested.contains(QLatin1String(fallback))) {
      return nested.value(QLatin1String(fallback)).toDouble();
    }
    return payload.value(QLatin1String(fallback)).toDouble();
  }
  return 0.0;
}

} // namespace

SessionUsage MergeSessionUsage(const SessionUsage &usage,
                               const QJsonObject &payload) {
  SessionUsage merged = usage;
  const QJsonObject nested = payload.value(QStringLiteral("usage")).toObject();

  if (Has(nested, payload, "context_used")) {
    merged.context_used =
        static_cast<qint64>(Read(nested, payload, "context_used"));
  } else if (Has(nested, payload, "context_tokens")) {
    merged.context_used =
        static_cast<qint64>(Read(nested, payload, "context_tokens"));
  } else if (Has(nested, payload, "prompt_tokens")) {
    merged.context_used =
        static_cast<qint64>(Read(nested, payload, "prompt_tokens"));
  }
  if (Has(nested, payload, "context_max")) {
    merged.context_max =
        static_cast<qint64>(Read(nested, payload, "context_max"));
  } else if (Has(nested, payload, "context_length")) {
    merged.context_max =
        static_cast<qint64>(Read(nested, payload, "context_length"));
  } else if (Has(nested, payload, "config_context_length")) {
    merged.context_max =
        static_cast<qint64>(Read(nested, payload, "config_context_length"));
  }
  if (Has(nested, payload, "input")) {
    merged.input = static_cast<qint64>(Read(nested, payload, "input"));
  } else if (Has(nested, payload, "input_tokens")) {
    merged.input = static_cast<qint64>(Read(nested, payload, "input_tokens"));
  }
  if (Has(nested, payload, "output")) {
    merged.output = static_cast<qint64>(Read(nested, payload, "output"));
  } else if (Has(nested, payload, "output_tokens")) {
    merged.output = static_cast<qint64>(Read(nested, payload, "output_tokens"));
  }
  if (Has(nested, payload, "total")) {
    merged.total = static_cast<qint64>(Read(nested, payload, "total"));
  } else if (Has(nested, payload, "total_tokens")) {
    merged.total = static_cast<qint64>(Read(nested, payload, "total_tokens"));
  }
  if (Has(nested, payload, "calls")) {
    merged.calls = static_cast<int>(Read(nested, payload, "calls"));
  } else if (Has(nested, payload, "api_calls")) {
    merged.calls = static_cast<int>(Read(nested, payload, "api_calls"));
  }
  if (Has(nested, payload, "compressions")) {
    merged.compressions =
        static_cast<int>(Read(nested, payload, "compressions"));
  } else if (Has(nested, payload, "compression_count")) {
    merged.compressions =
        static_cast<int>(Read(nested, payload, "compression_count"));
  }
  if (Has(nested, payload, "context_percent")) {
    merged.context_percent =
        static_cast<int>(Read(nested, payload, "context_percent"));
  } else if (merged.context_max > 0) {
    merged.context_percent = static_cast<int>(
        std::min<qint64>(100, merged.context_used * 100 / merged.context_max));
  }
  if (Has(nested, payload, "context_estimated")) {
    merged.context_estimated =
        (nested.contains(QStringLiteral("context_estimated")) ? nested
                                                              : payload)
            .value(QStringLiteral("context_estimated"))
            .toBool();
  }
  return merged;
}

} // namespace hermes::ui
