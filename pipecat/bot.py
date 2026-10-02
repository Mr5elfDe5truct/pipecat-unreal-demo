#
# Copyright (c) 2026, Daily
#
# SPDX-License-Identifier: BSD-2-Clause
#

"""The orb's bot: a Gemini Live voice agent that reshapes the game's world.

The game connects to it over a WebSocket. When the player asks for a change,
Gemini calls ``set_environment``, which the game receives, with its arguments,
and applies.

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
from pipecat.processors.frameworks.rtvi import RTVIFunctionCallReportLevel, RTVIObserverParams
from pipecat.runner.types import RunnerArguments
from pipecat.runner.utils import create_transport
from pipecat.serializers.protobuf import ProtobufFrameSerializer
from pipecat.services.google.gemini_live.llm import GeminiLiveLLMService
from pipecat.services.llm_service import FunctionCallParams
from pipecat.transports.base_transport import BaseTransport
from pipecat.transports.websocket.fastapi import FastAPIWebsocketParams
from pipecat.workers.runner import WorkerRunner

load_dotenv(override=True)

SYSTEM_INSTRUCTION = """
You are a small glowing spirit, an orb floating next to the player in a world
that starts empty: a flat grid under a clear midday sky.

You can reshape the world with the set_environment function: the time of day,
the fog and the clouds. When the player asks for a change, call it with only
what changes, and then say in a few words what you did. If they ask for
something you can't do yet, say so, and suggest something you can.

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
    },
    required=[],
)

transport_params = {
    "websocket": lambda: FastAPIWebsocketParams(
        audio_in_enabled=True,
        audio_out_enabled=True,
        add_wav_header=False,
        serializer=ProtobufFrameSerializer(),
    ),
}


async def run_bot(transport: BaseTransport, runner_args: RunnerArguments):
    """Runs the bot with the given transport."""
    # What the world looks like, as the game starts it.
    environment = {"time_of_day": "noon", "fog": "none", "clouds": "clear"}

    async def set_environment(params: FunctionCallParams):
        # The game applies the change when it's told about the call. Tell the
        # LLM what the world looks like now.
        environment.update({k: v for k, v in params.arguments.items() if k in environment})
        logger.info(f"Environment: {environment}")
        await params.result_callback(environment)

    llm = GeminiLiveLLMService(
        api_key=os.getenv("GOOGLE_API_KEY"),
        settings=GeminiLiveLLMService.Settings(
            voice="Puck",
            system_instruction=SYSTEM_INSTRUCTION,
        ),
        tools=ToolsSchema(standard_tools=[SET_ENVIRONMENT]),
    )
    llm.register_function("set_environment", set_environment)

    context = LLMContext(
        [{"role": "user", "content": "Greet the player in one short sentence."}],
    )
    user_aggregator, assistant_aggregator = LLMContextAggregatorPair(
        context,
        user_params=LLMUserAggregatorParams(vad_analyzer=SileroVADAnalyzer()),
    )

    pipeline = Pipeline(
        [
            transport.input(),
            user_aggregator,
            llm,
            transport.output(),
            assistant_aggregator,
        ]
    )

    worker = PipelineWorker(
        pipeline,
        params=PipelineParams(enable_metrics=True, enable_usage_metrics=True),
        # The game needs the function's name and arguments to apply it.
        rtvi_observer_params=RTVIObserverParams(
            function_call_report_level={"set_environment": RTVIFunctionCallReportLevel.FULL}
        ),
    )

    @worker.rtvi.event_handler("on_client_ready")
    async def on_client_ready(rtvi):
        await worker.queue_frames([LLMRunFrame()])

    @transport.event_handler("on_client_disconnected")
    async def on_client_disconnected(transport, client):
        await worker.cancel()

    runner = WorkerRunner(handle_sigint=runner_args.handle_sigint)
    await runner.add_workers(worker)
    await runner.run()


async def bot(runner_args: RunnerArguments):
    """The bot's entry point, for Pipecat's development runner."""
    transport = await create_transport(runner_args, transport_params)
    await run_bot(transport, runner_args)


if __name__ == "__main__":
    from pipecat.runner.run import main

    main()
