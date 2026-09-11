"""Regression cases for argument wrapping; run explicitly when validation is requested."""

from pathlib import Path
import os
import shutil
import unittest

from format_cpp import format_argument_lists, format_source

CLANG_FORMAT = os.environ.get("CLANG_FMT") or shutil.which("clang-format")


class ArgumentLayoutTests(unittest.TestCase):
    def test_up_to_three_arguments_stay_together_beyond_200_columns(self):
        for count in (1, 2, 3):
            with self.subTest(count=count):
                arguments = [f"argument{index}_" + "x" * 210 for index in range(count)]
                source = "    Call(\n        " + ",\n        ".join(arguments) + "\n    );\n"
                expected = "    Call( " + ", ".join(arguments) + " );\n"
                self.assertEqual(format_argument_lists(source), expected)

    def test_four_short_arguments_stay_together(self):
        self.assertEqual(
            format_argument_lists("Call(\n    first,\n    second,\n    third,\n    fourth\n);\n"),
            "Call( first, second, third, fourth );\n",
        )

    def test_four_long_arguments_align_beneath_the_first(self):
        arguments = [f"argument{index}_" + "x" * 60 for index in range(4)]
        source = "    Call( " + ", ".join(arguments) + " );\n"
        expected = "    Call( " + (",\n" + " " * 10).join(arguments) + " );\n"
        self.assertEqual(format_argument_lists(source), expected)
        self.assertEqual(format_argument_lists(expected), expected)

    def test_parameter_declaration_uses_same_count_rule(self):
        arguments = ["const LongType& " + "parameter" * 12 for _ in range(3)]
        source = "void Function(\n    " + ",\n    ".join(arguments) + "\n);\n"
        self.assertEqual(format_argument_lists(source), "void Function( " + ", ".join(arguments) + " );\n")

    def test_nested_template_commas_are_not_arguments(self):
        first = "Make<std::pair<int, int>, std::array<int, 4>>( value )"
        second = "second_" + "x" * 180
        source = f"Call( {first},\n    {second},\n    third );\n"
        self.assertEqual(format_argument_lists(source), f"Call( {first}, {second}, third );\n")

    def test_literal_commas_and_parentheses_are_not_syntax(self):
        literal = 'R"tag(a, b, c, d, ) " raw)tag"'
        source = f"Call( {literal},\n    second,\n    third );\n"
        self.assertEqual(format_argument_lists(source), f"Call( {literal}, second, third );\n")

    def test_comments_and_directives_are_preserved(self):
        for source in (
            "Call( first, // why this argument is used\n    second );\n",
            "Call( first, /* explanation */\n    second );\n",
            "Call( first,\n#if ENABLED\n    second\n#else\n    third\n#endif\n);\n",
            "#define CALL(a, b) Target(a, \\\n    b)\n",
            "// clang-format off\nCall( first,\n    second );\n// clang-format on\n",
        ):
            with self.subTest(source=source):
                self.assertEqual(format_argument_lists(source), source)

    def test_controls_are_not_parameter_lists(self):
        source = "if ( first &&\n     second )\n{\n}\n"
        self.assertEqual(format_argument_lists(source), source)

    def test_nested_expanded_call_keeps_its_own_layout(self):
        arguments = [f"argument{index}_" + "x" * 60 for index in range(4)]
        source = "Outer( Inner( " + ", ".join(arguments) + " ),\n    last );\n"
        result = format_argument_lists(source)
        self.assertIn("Outer( Inner( " + arguments[0], result)
        self.assertTrue(result.endswith(", last );\n"))
        for argument in arguments[1:]:
            self.assertIn("\n" + " " * 14 + argument, result)

    def test_multiline_lambda_body_is_not_flattened(self):
        source = "Call( []()\n    {\n        return 1;\n    },\n    second );\n"
        result = format_argument_lists(source)
        self.assertIn("{\n        return 1;\n    }", result)
        self.assertIn("}, second );", result)

    def test_enclosing_function_body_does_not_protect_call_whitespace(self):
        source = "void Example()\n{\n    Call( first,\n        second );\n}\n"
        self.assertEqual(format_argument_lists(source), "void Example()\n{\n    Call( first, second );\n}\n")

    def test_requested_reserve_initializer_layout(self):
        prefix = "const CoreAllocation::RuntimeReserveGrowthRequest request = { "
        values = ["REPLAY_RECORDER_SAMPLE_RESERVE_OWNER", "targetName",
                  "CoreAllocation::RuntimeReservePhase::Replay", "0", "static_cast<int>( oldBytes )",
                  "static_cast<int>( allocationBytes )", "1", "allocationBytes"]
        source = prefix.replace("= { ", "=\n    { ") + ", ".join(values) + " };\n"
        expected = prefix + (",\n" + " " * len(prefix)).join(values) + " };\n"
        self.assertEqual(format_argument_lists(source), expected)
        self.assertEqual(format_argument_lists(expected), expected)

    def test_short_initializers_stay_together_beyond_200_columns(self):
        for count in (1, 2, 3):
            with self.subTest(count=count):
                values = [f"value{index}_" + "x" * 210 for index in range(count)]
                source = "Value value =\n    { " + ",\n      ".join(values) + " };\n"
                self.assertEqual(format_argument_lists(source), "Value value = { " + ", ".join(values) + " };\n")

    def test_direct_initialization_and_trailing_comma(self):
        values = [f"value{index}_" + "x" * 60 for index in range(4)]
        prefix = "Value value { "
        source = prefix + ", ".join(values) + ", };\n"
        expected = prefix + (",\n" + " " * len(prefix)).join(values) + ", };\n"
        self.assertEqual(format_argument_lists(source), expected)

    def test_code_and_type_bodies_are_not_initializers(self):
        source = "namespace Example\n{\nstruct Empty\n{\n};\nenum class Mode\n{\n    A, B, C, D\n};\nvoid Run()\n{\n}\nauto TrailingReturn() -> void\n{\n    if ( enabled )\n    {\n    }\n}\n}\n"
        self.assertEqual(format_argument_lists(source), source)


@unittest.skipUnless(CLANG_FORMAT, "clang-format must be available for pipeline checks")
class FormatterPipelineTests(unittest.TestCase):
    def test_adjacent_trailing_comments_reach_stable_layout(self):
        path = Path(__file__).resolve().parents[1] / "SkullbonezSource" / "FormattingFixture.h"
        source = (
            "struct Example {\n"
            "static void RenderText(TextBatch& batch, float x, float y, float size, "
            "float r, float g, float b, const char* format, ...); // Queues colored SDF text for this frame's text batch.\n"
            "static void FlushText(TextBatch& batch, Rendering::Dx12TextureOwner& renderTextures, "
            "Rendering::Dx12GeometryOwner& renderCommands); // Uploads the current queued text segment.\n"
            "};\n"
        ).encode()
        clang_format = CLANG_FORMAT
        result = format_source(source, path, clang_format)
        self.assertEqual(format_source(result, path, clang_format), result)

    def test_complete_pipeline_is_stable(self):
        clang_format = CLANG_FORMAT
        path = Path(__file__).resolve().parents[1] / "SkullbonezSource" / "FormattingFixture.cpp"
        for count in (1, 2, 3, 4):
            with self.subTest(count=count):
                arguments = [f"argument{index}_" + "x" * 210 for index in range(count)]
                source = ("void Example() { Call(" + ",".join(arguments) + "); }\n").encode()
                result = format_source(source, path, clang_format)
                self.assertEqual(format_source(result, path, clang_format), result)
                if count <= 3:
                    self.assertIn(("Call( " + ", ".join(arguments) + " );").encode(), result)
                else:
                    self.assertIn(("Call( " + arguments[0]).encode(), result)
                    for argument in arguments[1:]:
                        self.assertIn(("\n" + " " * 10 + argument).encode(), result)

    def test_requested_initializer_through_complete_pipeline(self):
        path = Path(__file__).resolve().parents[1] / "SkullbonezSource" / "FormattingFixture.cpp"
        prefix = "const CoreAllocation::RuntimeReserveGrowthRequest request = { "
        values = ["REPLAY_RECORDER_SAMPLE_RESERVE_OWNER", "targetName",
                  "CoreAllocation::RuntimeReservePhase::Replay", "0", "static_cast<int>( oldBytes )",
                  "static_cast<int>( allocationBytes )", "1", "allocationBytes"]
        source = (prefix.replace("= { ", "=\n    { ") + ", ".join(values) + " };\n").encode()
        expected = (prefix + (",\n" + " " * len(prefix)).join(values) + " };\n").encode()
        self.assertEqual(format_source(source, path, CLANG_FORMAT), expected)


if __name__ == "__main__":
    unittest.main()
