#include <Arduino.h>

#include "kira_scenes.h"
#include "elli_visual.h"


String normalizeInput(String s);
void elliSay(const String& s);


namespace {


bool equalsAny(
  const String& q,
  const char* const values[],
  size_t count
) {

  for(
    size_t i = 0;
    i < count;
    i++
  ) {

    if(
      q ==
      values[i]
    ) {

      return true;
    }
  }

  return false;
}


void setAll(
  bool value,
  bool& mainLightOn,
  bool& fanOn,
  bool& chargerOn,
  bool& secondLightOn
) {

  mainLightOn =
    value;

  fanOn =
    value;

  chargerOn =
    value;

  secondLightOn =
    value;
}


} // namespace


void kiraScenesBegin() {

  Serial.println(
    "[SCENES 5H] Automation scenes ready."
  );

  Serial.println(
    "[SCENES 5H] study / leaving / night / morning / sleep"
  );
}


bool kiraSceneHandleCommand(
  String q,
  bool& mainLightOn,
  bool& fanOn,
  bool& chargerOn,
  bool& secondLightOn
) {

  q =
    normalizeInput(
      q
    );


  // ==========================================================
  // STUDY SCENE
  // ==========================================================
  //
  // Main light ON.
  // Fan logical state ON.
  // Charger and second light remain unchanged.
  //
  // Only the main-light relay is physically connected today.
  // ==========================================================

  const char* const studyScene[] = {

    "study scene",
    "start study scene",
    "study setup",
    "start study setup",
    "study mode",
    "start study",
    "focus scene",
    "start focus scene",
    "focus mode",
    "lets study",
    "let us study"
  };


  if(
    equalsAny(
      q,
      studyScene,
      sizeof(
        studyScene
      ) /
      sizeof(
        studyScene[0]
      )
    )
  ) {

    mainLightOn =
      true;

    fanOn =
      true;


    elliSay(
      "Study scene ready. Main light is on and the fan state is on."
    );


    return true;
  }


  // ==========================================================
  // LEAVING SCENE
  // ==========================================================

  const char* const leavingScene[] = {

    "leaving scene",
    "start leaving scene",
    "leaving mode",
    "i am leaving",
    "i am going out",
    "going out",
    "leaving home",
    "away scene",
    "away mode",
    "start away scene"
  };


  if(
    equalsAny(
      q,
      leavingScene,
      sizeof(
        leavingScene
      ) /
      sizeof(
        leavingScene[0]
      )
    )
  ) {

    setAll(
      false,
      mainLightOn,
      fanOn,
      chargerOn,
      secondLightOn
    );


    elliSay(
      "Leaving scene set. All devices are off."
    );


    return true;
  }


  // ==========================================================
  // GOOD NIGHT SCENE
  // ==========================================================

  const char* const nightScene[] = {

    "good night scene",
    "night scene",
    "start night scene",
    "night mode",
    "bedtime scene",
    "bedtime mode",
    "start bedtime scene"
  };


  if(
    equalsAny(
      q,
      nightScene,
      sizeof(
        nightScene
      ) /
      sizeof(
        nightScene[0]
      )
    )
  ) {

    setAll(
      false,
      mainLightOn,
      fanOn,
      chargerOn,
      secondLightOn
    );


    elliSay(
      "Good night scene set. All devices are off. I will get sleepy now."
    );


    // Finish the response animation first, then stay sleepy.
    elliVisualSleepAfterSpeech();


    return true;
  }


  // ==========================================================
  // MORNING SCENE
  // ==========================================================

  const char* const morningScene[] = {

    "morning scene",
    "start morning scene",
    "morning mode",
    "good morning scene",
    "start morning",
    "wake scene",
    "start wake scene"
  };


  if(
    equalsAny(
      q,
      morningScene,
      sizeof(
        morningScene
      ) /
      sizeof(
        morningScene[0]
      )
    )
  ) {

    mainLightOn =
      true;

    fanOn =
      false;

    chargerOn =
      false;

    secondLightOn =
      false;


    elliSay(
      "Morning scene ready. Main light is on."
    );


    return true;
  }


  // ==========================================================
  // ELLI SLEEP - VISUAL ONLY
  // ==========================================================

  const char* const sleepScene[] = {

    "sleep scene",
    "sleep mode",
    "elli sleep",
    "go to sleep",
    "get sleepy",
    "sleep now"
  };


  if(
    equalsAny(
      q,
      sleepScene,
      sizeof(
        sleepScene
      ) /
      sizeof(
        sleepScene[0]
      )
    )
  ) {

    elliSay(
      "Okay. I will stay sleepy until you need me again."
    );


    elliVisualSleepAfterSpeech();


    return true;
  }


  return false;
}
