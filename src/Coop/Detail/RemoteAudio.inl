        struct ProxyAudioInstance
        {
            UAudioComponent* component = nullptr;
            int objectIndex = -1;
            USoundCue* cue = nullptr;
            AActor* source = nullptr;
            int sourceIndex = -1;
            std::uint32_t id = 0;
            std::uint32_t lastSerial = 0;
            bool followsPawn = false;
            Clock::time_point nextSend{}, expires{};
        };
        std::unordered_map<UAudioComponent*, ProxyAudioInstance> g_localWeaponAudio;
        std::unordered_map<std::uint32_t, ProxyAudioInstance> g_remoteWeaponAudio;
        std::unordered_map<std::uint32_t, Clock::time_point> g_stoppedRemoteAudio;
        std::unordered_map<std::uint32_t, USoundCue*> g_proxySoundCues;
        std::unordered_map<std::uint32_t, Clock::time_point> g_missingProxySoundUntil;
        std::vector<ProxyAudioInstance> g_remoteNotifyAudio;
        std::uint32_t g_localAudioId = 1;

        bool IsAudioObject(const ProxyAudioInstance& sound)
        {
            auto* objects = UObject::GObjObjects();
            return objects && sound.objectIndex >= 0 && sound.objectIndex < objects->size()
                && objects->at(sound.objectIndex) == sound.component
                && !(sound.component->ObjectFlags & (RF_PendingKill | RF_Unreachable | RF_BeginDestroyed | RF_FinishDestroyed))
                && sound.component->SoundCue == sound.cue;
        }

        bool NativeAudioPlaying(UAudioComponent* component)
        {
            static_assert(sizeof(UAudioComponent_execIsPlaying_Params) == 4,
                "UE3 IsPlaying returns a four-byte UBOOL, never a C++ bool");
            UAudioComponent_execIsPlaying_Params params{};
            auto* function = UFunction::FindFunction("Function Engine.AudioComponent.IsPlaying");
            if (!function || function->ParmsSize != sizeof(params)
                || function->ReturnValueOffset != 0) return false;
            return InvokeNativeFunction(component, function, params)
                && params.ReturnValue;
        }

        void StopProxyAudio(ProxyAudioInstance& sound)
        {
            if (!IsAudioObject(sound)) return;
            UAudioComponent_execStop_Params params{};
            InvokeNativeFunction(sound.component, UFunction::FindFunction("Function Engine.AudioComponent.Stop"), params);
        }

        // Every capture path validates ownership; NPCs, UI/music and the
        // receiving presentation never enter the outgoing audio stream.
        AActor* LocalWeaponAudioSource(UObject* object)
        {
            if (!g_config.enabled || g_config.localMirror || g_soloMinigameActive
                || g_remoteParticleReplayDepth || !object || !g_currentWorld) return nullptr;
            auto* local = GetLocalPawn();
            if (!local || !object->IsA(AActor::StaticClass())) return nullptr;
            auto* actor = reinterpret_cast<AActor*>(object);
            if (actor->WorldInfo != g_currentWorld) return nullptr;
            if (object->IsA(AWeaponForAlice::StaticClass()))
                return actor->Instigator == local ? actor : nullptr;
            if (object->IsA(AAliceGameProjectile::StaticClass()))
            {
                auto* projectile = reinterpret_cast<AAliceGameProjectile*>(object);
                return projectile->Instigator == local || (projectile->WeaponOwner
                    && projectile->WeaponOwner->Instigator == local) ? actor : nullptr;
            }
            if (object->IsA(AAliceClonePawn::StaticClass()))
                return actor->Instigator == local || actor->Owner == local
                    || local->MyClonePawn == actor ? actor : nullptr;
            return nullptr;
        }

        void SendNativeWeaponAudio(ProjectileEventKind kind, AActor* source,
            UAudioComponent* component, USoundCue* cue, std::uint32_t id, bool follows)
        {
            if (!source || !cue) return;
            ProjectileEventPayload event{};
            event.kind = kind;
            event.projectileId = id;
            event.particleTemplateHash = HashMapName(cue->GetFullName());
            event.variant = follows ? 1 : 0;
            event.reserved = component ? 1 : 0; // heartbeat-controlled lifetime
            FVector location = component && !component->bUseOwnerLocation && !follows
                ? component->Location : source->Location;
            // Attached audio can retain a zero local Location; never interpret
            // that as the world origin for a distant weapon or clock bomb.
            if (location.SizeSquared() < 0.001f && source->Location.SizeSquared() > 1.0f)
                location = source->Location;
            event.location[0] = location.X; event.location[1] = location.Y; event.location[2] = location.Z;
            event.extra = component ? component->VolumeMultiplier : 1.0f;
            event.velocity[0] = component ? component->PitchMultiplier : 1.0f;
            if (component)
            {
                // Weapon loops commonly drive one authored pitch/intensity
                // parameter. Preserve its native value as well as multipliers.
                for (int i = 0; i < component->InstanceParameters.size() && i < 16; ++i)
                {
                    const auto& parameter = component->InstanceParameters.at(i);
                    if (parameter.WaveParam || !parameter.ParamName.IsValid() || !std::isfinite(parameter.FloatParam)) continue;
                    const auto name = parameter.ParamName.ToString();
                    if (name.empty() || name.size() >= sizeof(event.effectParameterName)) continue;
                    strcpy_s(event.effectParameterName, name.c_str()); event.effectParameters[0] = parameter.FloatParam;
                    event.reserved |= 2; break;
                }
            }
            QueueProjectileEvent(event);
            static bool logged = false;
            if (!logged && kind == ProjectileEventKind::NativeSoundStart)
            {
                logged = true;
                Log("NATIVEAUDIO first TX cue=" + ObjectName(cue)
                    + ", source=" + ObjectName(source) + ", tracked=" + (component ? "yes." : "no."));
            }
        }

        void CaptureLocalWeaponAudio(UObject* source, UAudioComponent* component)
        {
            auto* actor = LocalWeaponAudioSource(source);
            if (!actor || !component || (component->ObjectFlags & (RF_PendingKill | RF_Unreachable | RF_BeginDestroyed | RF_FinishDestroyed)) || !component->SoundCue
                || !component->IsA(UAudioComponent::StaticClass())) return;
            auto found = g_localWeaponAudio.find(component);
            if (found != g_localWeaponAudio.end() && IsAudioObject(found->second))
            {
                if (!found->second.id && NativeAudioPlaying(component))
                {
                    auto& sound = found->second;
                    sound.id = g_localAudioId++; if (!sound.id) sound.id = g_localAudioId++;
                    SendNativeWeaponAudio(ProjectileEventKind::NativeSoundStart, actor, component,
                        sound.cue, sound.id, sound.followsPawn);
                    sound.nextSend = Clock::now() + std::chrono::milliseconds(100);
                }
                return;
            }
            if (found != g_localWeaponAudio.end())
            {
                if (found->second.id)
                {
                    ProjectileEventPayload stop{}; stop.kind = ProjectileEventKind::NativeSoundStop;
                    stop.projectileId = found->second.id; QueueProjectileEvent(stop);
                }
                g_localWeaponAudio.erase(found);
            }
            if (g_localWeaponAudio.size() >= 64) return;
            ProxyAudioInstance sound{};
            sound.component = component; sound.objectIndex = component->ObjectInternalInteger;
            sound.cue = component->SoundCue; sound.followsPawn = actor->IsA(AWeaponForAlice::StaticClass());
            sound.source = actor; sound.sourceIndex = actor->ObjectInternalInteger;
            // id=0 until native Play has actually begun (Create may use bPlay=false).
            sound.nextSend = Clock::now();
            sound.expires = sound.nextSend + std::chrono::seconds(10);
            if (NativeAudioPlaying(component))
            {
                sound.id = g_localAudioId++; if (!sound.id) sound.id = g_localAudioId++;
                SendNativeWeaponAudio(ProjectileEventKind::NativeSoundStart, actor, component, sound.cue, sound.id, sound.followsPawn);
                sound.nextSend += std::chrono::milliseconds(100);
            }
            g_localWeaponAudio[component] = sound;
        }

        void TickLocalWeaponAudio()
        {
            const auto now = Clock::now();
            for (auto it = g_localWeaponAudio.begin(); it != g_localWeaponAudio.end();)
            {
                auto& sound = it->second;
                const bool valid = IsAudioObject(sound);
                if (valid && now < sound.nextSend) { ++it; continue; }
                auto* component = sound.component;
                auto* objects = UObject::GObjObjects();
                AActor* source = valid && objects && sound.sourceIndex >= 0 && sound.sourceIndex < objects->size()
                    && objects->at(sound.sourceIndex) == sound.source ? LocalWeaponAudioSource(sound.source) : nullptr;
                const bool playing = valid && NativeAudioPlaying(component);
                if (!playing || !source)
                {
                    if (sound.id)
                    {
                        ProjectileEventPayload event{}; event.kind = ProjectileEventKind::NativeSoundStop;
                        event.projectileId = sound.id; QueueProjectileEvent(event);
                    }
                    if (!valid || sound.id || !source || now >= sound.expires) it = g_localWeaponAudio.erase(it);
                    else { sound.nextSend = now + std::chrono::milliseconds(50); ++it; }
                    continue;
                }
                const auto kind = sound.id ? ProjectileEventKind::NativeSoundUpdate : ProjectileEventKind::NativeSoundStart;
                if (!sound.id) { sound.id = g_localAudioId++; if (!sound.id) sound.id = g_localAudioId++; }
                SendNativeWeaponAudio(kind, source, component, sound.cue, sound.id, sound.followsPawn);
                sound.nextSend = now + std::chrono::milliseconds(100);
                ++it;
            }
        }

        USoundCue* ResolveProxySound(std::uint32_t hash)
        {
            if (!hash) return nullptr;
            const auto found = g_proxySoundCues.find(hash);
            if (found != g_proxySoundCues.end() && IsLiveUObject(found->second)) return found->second;
            if (g_missingProxySoundUntil.contains(hash) && Clock::now() < g_missingProxySoundUntil[hash]) return nullptr;
            auto* objects = UObject::GObjObjects();
            if (!objects) return nullptr;
            for (int i = 0; i < objects->size(); ++i)
            {
                auto* object = objects->at(i);
                if (object && object->IsA(USoundCue::StaticClass())
                    && HashMapName(object->GetFullName()) == hash)
                {
                    auto* cue = reinterpret_cast<USoundCue*>(object);
                    g_proxySoundCues[hash] = cue; return cue;
                }
            }
            g_missingProxySoundUntil[hash] = Clock::now() + std::chrono::milliseconds(500);
            return nullptr;
        }

        void ApplyProxyAudioParameter(UAudioComponent* component, const ProjectileEventPayload& event)
        {
            if (!(event.reserved & 2) || !event.effectParameterName[0]
                || !memchr(event.effectParameterName, 0, sizeof(event.effectParameterName))
                || !std::isfinite(event.effectParameters[0])) return;
            UAudioComponent_execSetFloatParameter_Params parameter{};
            parameter.InName = FName(event.effectParameterName); parameter.InFloat = event.effectParameters[0];
            InvokeNativeFunction(component, UFunction::FindFunction("Function Engine.AudioComponent.SetFloatParameter"), parameter);
        }

        UAudioComponent* PlayProxySound(USoundCue* cue, const FVector& location,
            float volume = 1.0f, float pitch = 1.0f, const ProjectileEventPayload* event = nullptr)
        {
            if (!g_currentWorld || !cue || !std::isfinite(volume) || !std::isfinite(pitch)
                || !std::isfinite(location.X) || !std::isfinite(location.Y) || !std::isfinite(location.Z)) return nullptr;
            RemoteParticleReplayScope scope;
            AActor_execCreateAudioComponent_Params create{};
            create.InSoundCue = cue; create.bStopWhenOwnerDestroyed = true;
            create.bUseLocation = true; create.SourceLocation = location;
            if (!InvokeNativeFunction(g_currentWorld,
                UFunction::FindFunction("Function Engine.Actor.CreateAudioComponent"), create) || !create.ReturnValue) return nullptr;
            auto* component = create.ReturnValue;
            component->bAutoDestroy = true;
            component->bUseOwnerLocation = false;
            component->bAllowSpatialization = true;
            component->bIsUISound = false;
            component->VolumeMultiplier = std::clamp(volume, 0.0f, 4.0f);
            component->PitchMultiplier = std::clamp(pitch, 0.25f, 4.0f);
            if (event) ApplyProxyAudioParameter(component, *event);
            UAudioComponent_execPlay_Params play{};
            if (!InvokeNativeFunction(component, UFunction::FindFunction("Function Engine.AudioComponent.Play"), play)) return nullptr;
            return component;
        }

        void StopRemoteWeaponAudio(bool all)
        {
            for (auto& sound : g_remoteNotifyAudio) StopProxyAudio(sound);
            g_remoteNotifyAudio.clear();
            for (auto it = g_remoteWeaponAudio.begin(); it != g_remoteWeaponAudio.end();)
            {
                if (!all && !it->second.followsPawn) { ++it; continue; }
                StopProxyAudio(it->second);
                if (!all) g_stoppedRemoteAudio[it->first] = Clock::now();
                it = g_remoteWeaponAudio.erase(it);
            }
            if (all) { g_stoppedRemoteAudio.clear(); g_proxySoundCues.clear(); g_missingProxySoundUntil.clear(); g_localWeaponAudio.clear(); }
        }

        void ApplyRemoteAudioEvent(const ProjectileEventPayload& event)
        {
            const auto now = Clock::now();
            if (event.kind == ProjectileEventKind::NativeSoundStop)
            {
                if (auto found = g_remoteWeaponAudio.find(event.projectileId); found != g_remoteWeaponAudio.end())
                { StopProxyAudio(found->second); g_remoteWeaponAudio.erase(found); }
                g_stoppedRemoteAudio[event.projectileId] = now; return;
            }
            if (g_stoppedRemoteAudio.contains(event.projectileId)) return;
            if (!std::isfinite(event.extra) || !std::isfinite(event.velocity[0])
                || event.variant > 1 || (event.reserved & ~3u)) return;
            if (event.variant && (!g_remotePawn || g_remoteRenderDetached || g_remotePawn->Health <= 0)) return;
            const FVector location = event.variant && g_remotePawn ? g_remotePawn->Location
                : FVector(event.location[0], event.location[1], event.location[2]);
            auto found = g_remoteWeaponAudio.find(event.projectileId);
            if (found != g_remoteWeaponAudio.end() && !IsNewerSequence(event.eventSerial, found->second.lastSerial)) return;
            if (found == g_remoteWeaponAudio.end())
            {
                if (g_remoteWeaponAudio.size() >= 64) return;
                auto* cue = ResolveProxySound(event.particleTemplateHash);
                auto* component = PlayProxySound(cue, location, event.extra, event.velocity[0], &event);
                if (!component) return;
                ProxyAudioInstance sound{}; sound.component = component; sound.objectIndex = component->ObjectInternalInteger;
                sound.cue = cue; sound.id = event.projectileId; sound.followsPawn = event.variant != 0;
                sound.lastSerial = event.eventSerial;
                sound.expires = now + ((event.reserved & 1) ? std::chrono::milliseconds(600) : std::chrono::milliseconds(30000));
                g_remoteWeaponAudio[event.projectileId] = sound;
                static bool logged = false;
                if (!logged) { logged = true; Log("NATIVEAUDIO first RX cue=" + ObjectName(cue) + '.'); }
            }
            else
            {
                if (!IsAudioObject(found->second)) return;
                auto* component = found->second.component;
                component->Location = location;
                component->VolumeMultiplier = std::clamp(event.extra, 0.0f, 4.0f);
                component->PitchMultiplier = std::clamp(event.velocity[0], 0.25f, 4.0f);
                ApplyProxyAudioParameter(component, event);
                found->second.lastSerial = event.eventSerial;
                found->second.expires = now + std::chrono::milliseconds(600);
            }
        }

        void TickRemoteWeaponAudio()
        {
            const auto now = Clock::now();
            for (auto it = g_remoteNotifyAudio.begin(); it != g_remoteNotifyAudio.end();)
            {
                if (!IsAudioObject(*it) || now >= it->expires || !NativeAudioPlaying(it->component)
                    || (it->followsPawn && (!g_remotePawn || g_remoteRenderDetached)))
                { StopProxyAudio(*it); it = g_remoteNotifyAudio.erase(it); }
                else
                {
                    if (it->followsPawn) it->component->Location = g_remotePawn->Location;
                    ++it;
                }
            }
            for (auto it = g_remoteWeaponAudio.begin(); it != g_remoteWeaponAudio.end();)
            {
                auto& sound = it->second;
                if (!IsAudioObject(sound) || now >= sound.expires || !NativeAudioPlaying(sound.component)
                    || (sound.followsPawn && (!g_remotePawn || g_remoteRenderDetached || g_remotePawn->Health <= 0)))
                {
                    StopProxyAudio(sound); g_stoppedRemoteAudio[it->first] = now;
                    it = g_remoteWeaponAudio.erase(it); continue;
                }
                if (sound.followsPawn) sound.component->Location = g_remotePawn->Location;
                ++it;
            }
            for (auto it = g_stoppedRemoteAudio.begin(); it != g_stoppedRemoteAudio.end();)
                if (now - it->second > std::chrono::seconds(3)) it = g_stoppedRemoteAudio.erase(it); else ++it;
        }

        void ReplayProxySoundNotify(AAlicePawn* remote, UAnimNotify_Sound* notify)
        {
            if (!remote || g_remoteRenderDetached || !notify || !notify->SoundCue) return;
            const auto now = Clock::now();
            // Body, upper-body and weapon channels can carry the same cue on
            // the same rendered frame. Keep one instance of that authored sound.
            for (const auto& sound : g_remoteNotifyAudio)
                if (sound.cue == notify->SoundCue && now < sound.nextSend) return;
            if (g_remoteNotifyAudio.size() >= 32) return;
            auto* component = PlayProxySound(notify->SoundCue, remote->Location,
                notify->VolumeMultiplier, notify->PitchMultiplier);
            if (!component) return;
            ProxyAudioInstance sound{}; sound.component = component; sound.objectIndex = component->ObjectInternalInteger;
            sound.cue = notify->SoundCue; sound.followsPawn = notify->bFollowActor;
            sound.nextSend = now + std::chrono::milliseconds(35);
            // An unexpected looping asset must not survive a lost animation.
            sound.expires = now + std::chrono::seconds(15);
            g_remoteNotifyAudio.push_back(sound);
        }
