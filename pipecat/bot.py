#
# Copyright (c) 2026, Daily
#
# SPDX-License-Identifier: BSD-2-Clause
#

"""The orb's bot: a voice agent that reshapes the game's world.

``PlayerWorker`` talks to the player: it hears them with Deepgram, answers with
OpenAI, Gemini or a local Ollama model, and speaks with Cartesia or Deepgram,
depending on which keys are set. It owns the world: the settings the game
builds it from. When the player asks for a change, the LLM calls
``set_environment``, and the game gets the world's new settings, as an RTVI
server message. The game connects over a WebSocket, and only renders the world.

Run it with ``uv run bot.py -t websocket``.
"""

import os

from dotenv import load_dotenv
from loguru import logger
from pipecat.adapters.schemas.function_schema import FunctionSchema
from pipecat.adapters.schemas.tools_schema import ToolsSchema
from pipecat.audio.vad.silero import SileroVADAnalyzer
from pipecat.frames.frames import LLMRunFrame
from pipecat.pipeline.pipeline import Pipeline
from pipecat.pipeline.worker import PipelineParams, PipelineWorker
from pipecat.processors.aggregators.llm_context import LLMContext
from pipecat.processors.aggregators.llm_response_universal import (
    LLMContextAggregatorPair,
    LLMUserAggregatorParams,
)
from pipecat.runner.types import RunnerArguments
from pipecat.runner.utils import create_transport
from pipecat.serializers.protobuf import ProtobufFrameSerializer
from pipecat.services.cartesia.tts import CartesiaTTSService
from pipecat.services.deepgram.stt import DeepgramSTTService
from pipecat.services.llm_service import FunctionCallParams
from pipecat.services.openai.llm import OpenAILLMService
from pipecat.transports.base_transport import BaseTransport
from pipecat.transports.websocket.fastapi import FastAPIWebsocketParams
from pipecat.workers.runner import WorkerRunner

load_dotenv(override=True)

SYSTEM_INSTRUCTION = """
You are a small glowing spirit, an orb floating next to the player in a world
that starts empty: a flat grid under a clear midday sky.

You can reshape the world with the set_environment function: the time of day,
the fog, the clouds, the ground (flat, uneven with rolling hills, or with
mountains around the valley), grass, trees, and water: a pond, a lake and a
river, which can be there together. When the player asks for a change, call it
right away, with only what changes, without saying anything first. Once it
returns, say in a few words what you did, only once. If they ask for something
you can't do yet, say so, and suggest something you can.

You can also put objects in front of the player with the spawn_object function,
and take them all away with clear_objects. Pick the closest object you have for
what they ask for, and say what you made. You can make animals, dragons,
people, fires, weapons, rocks and trees, among other things. The player uses doors, levers, boxes
and people by walking up to them and pressing F, and the mouse wheel chooses
what to do.

Your output will be spoken, so don't use special characters. Keep your answers
to one or two short sentences.
"""

SET_ENVIRONMENT = FunctionSchema(
    name="set_environment",
    description=(
        "Changes the world around the player. Call it whenever the player asks to "
        "change the environment. Only include what changes."
    ),
    properties={
        "time_of_day": {"type": "string", "enum": ["dawn", "noon", "sunset", "night"]},
        "fog": {"type": "string", "enum": ["none", "light", "thick"]},
        "clouds": {"type": "string", "enum": ["clear", "some", "cloudy"]},
        "terrain": {
            "type": "string",
            "enum": ["flat", "hilly", "mountains"],
            "description": (
                "The ground: flat, hilly (uneven, with rolling hills), or mountains "
                "(hills, with mountains around the valley)."
            ),
        },
        "grass": {"type": "string", "enum": ["none", "some", "lush"]},
        "trees": {"type": "string", "enum": ["none", "few", "forest"]},
        "pond": {"type": "boolean", "description": "A small pond."},
        "lake": {"type": "boolean", "description": "A big lake."},
        "river": {"type": "boolean", "description": "A river, which flows through the lake."},
    },
    required=[],
)

# What the orb can make, which the game finds by these names in
# Config/DefaultGame.ini.
OBJECTS = {
    "door": "A door in a frame, which opens and closes.",
    "lever": "A lever, which can be pulled up or down.",
    "npc": "A person, who the player can talk to.",
    "interactive_box": "A box with things the player can do with it.",
    "jump_pad": "A glowing pad, which launches the player into the air.",
    "crate": "A plain crate, to climb on or build with.",
    "ramp": "A ramp, to walk up.",
    "pillar": "A tall round pillar.",
    "deer": "A stag with antlers, grazing.",
    "doe": "A female deer, grazing.",
    "fox": "A fox.",
    "pig": "A pig.",
    "wolf": "A wolf.",
    "crow": "A crow, a black bird.",
    "dragon": "A dragon, the size of a house.",
    "nightmare_dragon": "A dark, nightmarish dragon.",
    "terror_dragon": "A huge, terrifying dragon.",
    "giant_dragon": "The biggest dragon, as big as a castle.",
    "man": "A man in a jacket.",
    "man_2": "Another man, dressed differently.",
    "villager": "A person in casual clothes.",
    "villager_2": "Another person in casual clothes.",
    "villager_3": "Another person in casual clothes.",
    "villager_4": "Another person in casual clothes.",
    "campfire": "A campfire that lights up the area around it.",
    "bonfire": "A big bonfire.",
    "big_fire": "A large blaze of fire.",
    "torch_fire": "A small flame, like a torch's.",
    "smoke": "A column of smoke.",
    "rifle": "An assault rifle, lying on the ground.",
    "shotgun": "A shotgun, lying on the ground.",
    "pistol": "A pistol, lying on the ground.",
    "sniper_rifle": "A sniper rifle, lying on the ground.",
    "rocket_launcher": "A rocket launcher, lying on the ground.",
    "grenade_launcher": "A grenade launcher, lying on the ground.",
    "boulder": "A big boulder.",
    "rocks": "A group of small rocks.",
    "big_tree": "A huge old tree with big roots.",
}

SPAWN_OBJECT = FunctionSchema(
    name="spawn_object",
    description=(
        "Puts an object in front of the player. Call it once for each object they ask for."
    ),
    properties={
        "object": {
            "type": "string",
            "enum": list(OBJECTS),
            "description": "; ".join(f"{name}: {what}" for name, what in OBJECTS.items()),
        },
    },
    required=["object"],
)

CLEAR_OBJECTS = FunctionSchema(
    name="clear_objects",
    description="Removes every object you put in the world.",
    properties={},
    required=[],
)

# The world as it starts: an empty grid under a clear midday sky.
INITIAL_ENVIRONMENT = {
    "time_of_day": "noon",
    "fog": "none",
    "clouds": "clear",
    "terrain": "flat",
    "grass": "none",
    "trees": "none",
    "pond": False,
    "lake": False,
    "river": False,
}

transport_params = {
    "websocket": lambda: FastAPIWebsocketParams(
        audio_in_enabled=True,
        audio_out_enabled=True,
        add_wav_header=False,
        serializer=ProtobufFrameSerializer(),
    ),
}


def create_llm():
    """OpenAI or Gemini, whichever has a key, or else a local Ollama model."""
    if os.environ.get("OPENAI_API_KEY"):
        return OpenAILLMService(
            api_key=os.environ["OPENAI_API_KEY"],
            settings=OpenAILLMService.Settings(system_instruction=SYSTEM_INSTRUCTION),
        )
    if os.environ.get("GOOGLE_API_KEY"):
        from pipecat.services.google.llm import GoogleLLMService

        settings = GoogleLLMService.Settings(system_instruction=SYSTEM_INSTRUCTION)
        if os.environ.get("GOOGLE_MODEL"):
            settings.model = os.environ["GOOGLE_MODEL"]
        return GoogleLLMService(api_key=os.environ["GOOGLE_API_KEY"], settings=settings)

    from pipecat.services.ollama.llm import OLLamaLLMService

    return OLLamaLLMService(
        base_url=os.environ.get("OLLAMA_BASE_URL", "http://localhost:11434/v1"),
        settings=OLLamaLLMService.Settings(
            model=os.environ.get("OLLAMA_MODEL", "gemma4:e4b"),
            system_instruction=SYSTEM_INSTRUCTION,
            # Thinking models like Gemma 4 take seconds to think before each
            # answer, too slow for a conversation.
            extra={"reasoning_effort": "none"},
        ),
    )


def create_tts():
    """Cartesia if it has a key, or else Deepgram's own voices."""
    if os.environ.get("CARTESIA_API_KEY"):
        return CartesiaTTSService(
            api_key=os.environ["CARTESIA_API_KEY"],
            settings=CartesiaTTSService.Settings(voice=os.environ["CARTESIA_VOICE_ID"]),
        )

    from pipecat.services.deepgram.tts import DeepgramTTSService

    return DeepgramTTSService(
        api_key=os.environ["DEEPGRAM_API_KEY"],
        settings=DeepgramTTSService.Settings(
            voice=os.environ.get("DEEPGRAM_VOICE", "aura-2-draco-en")
        ),
    )


class PlayerWorker(PipelineWorker):
    """Talks to the player, and owns the world their game renders."""

    def __init__(self, transport: BaseTransport):
        stt = DeepgramSTTService(api_key=os.environ["DEEPGRAM_API_KEY"])

        llm = create_llm()
        llm.register_function("set_environment", self._set_environment)
        llm.register_function("spawn_object", self._spawn_object)
        llm.register_function("clear_objects", self._clear_objects)

        tts = create_tts()

        context = LLMContext(tools=ToolsSchema([SET_ENVIRONMENT, SPAWN_OBJECT, CLEAR_OBJECTS]))
        user_aggregator, assistant_aggregator = LLMContextAggregatorPair(
            context,
            user_params=LLMUserAggregatorParams(vad_analyzer=SileroVADAnalyzer()),
        )

        pipeline = Pipeline(
            [
                transport.input(),
                stt,
                user_aggregator,
                llm,
                tts,
                transport.output(),
                assistant_aggregator,
            ]
        )

        super().__init__(
            pipeline,
            name="player",
            params=PipelineParams(enable_metrics=True, enable_usage_metrics=True),
        )
        self._context = context
        self._environment = dict(INITIAL_ENVIRONMENT)

        self.rtvi.add_event_handler("on_client_ready", self._on_client_ready)
        transport.add_event_handler("on_client_disconnected", self._on_client_disconnected)

    async def _on_client_ready(self, rtvi):
        # The game renders the world the bot owns.
        await self._send_environment()
        self._context.add_message(
            {"role": "developer", "content": "Please introduce yourself to the user."}
        )
        await self.queue_frames([LLMRunFrame()])

    async def _on_client_disconnected(self, transport, client):
        await self.cancel()

    async def _set_environment(self, params: FunctionCallParams):
        self._environment.update(
            {k: v for k, v in params.arguments.items() if k in self._environment}
        )
        logger.info(f"Environment: {self._environment}")
        await self._send_environment()
        # Tell the LLM what the world looks like now.
        await params.result_callback(self._environment)

    async def _spawn_object(self, params: FunctionCallParams):
        name = params.arguments.get("object")
        if name not in OBJECTS:
            await params.result_callback({"error": f"There's no {name}."})
            return
        logger.info(f"Spawning a {name}")
        await self.rtvi.send_server_message({"type": "spawn", "object": name})
        await params.result_callback({"spawned": name})

    async def _clear_objects(self, params: FunctionCallParams):
        logger.info("Clearing the objects")
        await self.rtvi.send_server_message({"type": "clear"})
        await params.result_callback({"cleared": True})

    async def _send_environment(self):
        await self.rtvi.send_server_message(
            {"type": "environment", "environment": self._environment}
        )


async def run_bot(transport: BaseTransport, runner_args: RunnerArguments):
    """Runs the bot with the given transport."""
    runner = WorkerRunner(handle_sigint=runner_args.handle_sigint)
    await runner.add_workers(PlayerWorker(transport))
    await runner.run()


async def bot(runner_args: RunnerArguments):
    """The bot's entry point, for Pipecat's development runner."""
    transport = await create_transport(runner_args, transport_params)
    await run_bot(transport, runner_args)


if __name__ == "__main__":
    from pipecat.runner.run import main

    main()
