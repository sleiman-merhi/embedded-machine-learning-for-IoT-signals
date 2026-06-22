import numpy as np
import joblib

# ── Load saved model, scaler, encoder ────────────────────────────────────────
rf     = joblib.load("rf_model.pkl")
scaler = joblib.load("scaler.pkl")
le     = joblib.load("label_encoder.pkl")

print("Model loaded successfully.")
print("Classes:", list(le.classes_))

# ── Enter new sensor readings ───────────────────────────────────────────
# Format: [CO2, Humidity, Light, Motion, Smoke, Temperature]
# We can replace these values with real readings from Tera Term

test_samples = [
    # [CO2,  Humidity, Light,  Motion, Smoke, Temperature]
    [609,    38.0,     231.7,  1,      397,   24.0],   # expected: fire_alert
    [762,    50.1,     15.8,   1,      589,   5.9 ],   # expected: frost_risk
    [658,    80.6,     167.5,  0,      580,   24.1],   # expected: fungal_risk
    [714,    45.8,     612.5,  1,      645,   31.5],   # expected: irrigation_need
    [573,    54.8,     0.0,    1,      245,   23.3],   # expected: night_motion
    [561,    43.9,     139.2,  1,      446,   23.3],   # expected: normal
    [704,    61.2,     25.8,   0,      613,   21.8],   # expected: shading
]

# ── Predict ───────────────────────────────────────────────────────────────────
X = np.array(test_samples)
X_sc = scaler.transform(X)
predictions = rf.predict(X_sc)
probabilities = rf.predict_proba(X_sc)

print("\n{:<5} {:<25} {:<10}".format("Row", "Predicted Class", "Confidence"))
print("-" * 45)
for i, (pred, prob) in enumerate(zip(predictions, probabilities)):
    class_name = le.inverse_transform([pred])[0]
    confidence = prob.max() * 100
    print(f"{i+1:<5} {class_name:<25} {confidence:.1f}%")
