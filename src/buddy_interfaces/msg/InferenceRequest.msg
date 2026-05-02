# InferenceRequest.msg — brain → inference backends
string trigger_type           # "voice" or "emotion"
string user_text              # ASR text (voice trigger)
string emotion                # current emotion label
float32 emotion_confidence    # emotion confidence
string[] dialog_history       # recent N turns
string system_prompt          # system prompt text
sensor_msgs/Image image       # camera snapshot (optional)