// HazardNode Edge AI - Decision Tree Classifier
// Features: temp, hum, pitch, roll, smoke_analog
// Labels: 0=NORMAL, 1=WARNING, 2=HAZARD
// Generated from Scikit-Learn

#include <math.h>

int predict(float temp, float hum, float pitch, float roll, float smoke_analog) {
  float features[5] = {temp, hum, pitch, roll, smoke_analog};

  if (features[0] > 60.0000f) { // temp > 60
    return 2; // HAZARD
  } else {
    if (features[4] > 3000.0000f) { // smoke_analog > 3000
      return 2; // HAZARD
    } else {
      if (fabsf(features[2]) > 30.0000f) { // |pitch| > 30
        return 2; // HAZARD
      } else {
        if (fabsf(features[3]) > 30.0000f) { // |roll| > 30
          return 2; // HAZARD
        } else {
          if (features[0] > 45.0000f) { // temp > 45
            if (features[4] <= 3000.0000f) {
              return 1; // WARNING
            } else {
              return 2; // HAZARD
            }
          } else {
            if (features[4] > 1800.0000f) { // smoke_analog > 1800
              if (features[0] <= 60.0000f) {
                return 1; // WARNING
              } else {
                return 2; // HAZARD
              }
            } else {
              if (fabsf(features[2]) > 15.0000f) { // |pitch| > 15
                if (fabsf(features[2]) <= 30.0000f) {
                  return 1; // WARNING
                } else {
                  return 2; // HAZARD
                }
              } else {
                if (fabsf(features[3]) > 15.0000f) { // |roll| > 15
                  if (fabsf(features[3]) <= 30.0000f) {
                    return 1; // WARNING
                  } else {
                    return 2; // HAZARD
                  }
                } else {
                  if (features[0] <= 45.0000f) {
                    return 0; // NORMAL
                  } else {
                    return 1; // WARNING
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
