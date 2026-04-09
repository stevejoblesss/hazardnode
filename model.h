// HazardNode Edge AI - Decision Tree Classifier
// Features: temp, hum, pitch, roll, smoke_analog
// Labels: 0=NORMAL, 1=WARNING, 2=HAZARD

int predict(float temp, float hum, float pitch, float roll, float smoke_analog) {
  float features[5] = {temp, hum, pitch, roll, smoke_analog};

  if (features[0] <= 34.6880f) {  // temp <= 34.69
    if (features[3] <= -11.7350f) {  // roll <= -11.74
      if (features[0] <= 27.2500f) {  // temp <= 27.25
        if (features[4] <= 2231.0000f) {  // smoke_analog <= 2231.00
          if (features[2] <= -40.7600f) {  // pitch <= -40.76
            return 2;  // HAZARD
          } else {
            return 0;  // NORMAL
          }
        } else {
          return 2;  // HAZARD
        }
      } else {
        if (features[3] <= -22.5150f) {  // roll <= -22.52
          if (features[0] <= 29.5000f) {  // temp <= 29.50
            return 2;  // HAZARD
          } else {
            return 2;  // HAZARD
          }
        } else {
          return 0;  // NORMAL
        }
      }
    } else {
      if (features[4] <= 2292.0000f) {  // smoke_analog <= 2292.00
        if (features[2] <= -13.4400f) {  // pitch <= -13.44
          if (features[0] <= 25.7500f) {  // temp <= 25.75
            return 0;  // NORMAL
          } else {
            return 2;  // HAZARD
          }
        } else {
          if (features[2] <= 5.9000f) {  // pitch <= 5.90
            return 0;  // NORMAL
          } else {
            return 2;  // HAZARD
          }
        }
      } else {
        if (features[0] <= 25.8000f) {  // temp <= 25.80
          if (features[2] <= 36.7900f) {  // pitch <= 36.79
            return 0;  // NORMAL
          } else {
            return 2;  // HAZARD
          }
        } else {
          return 2;  // HAZARD
        }
      }
    }
  } else {
    if (features[4] <= 2008.5000f) {  // smoke_analog <= 2008.50
      return 1;  // WARNING
    } else {
      return 2;  // HAZARD
    }
  }
}