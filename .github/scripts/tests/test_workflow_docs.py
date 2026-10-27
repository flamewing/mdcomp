"""Keep README status badges aligned with workflow filenames."""
from pathlib import Path
import unittest


class WorkflowDocumentation(unittest.TestCase):
    def test_all_ci_badges_link_existing_workflows(self):
        import re
        root = Path(__file__).resolve().parents[3]
        readme = (root / "README.md").read_text()
        for workflow in (root / ".github/workflows").glob("ci-*.yml"):
            self.assertIn("/actions/workflows/" + workflow.name, readme)
        for workflow in re.findall(r"actions/workflows/([\w-]+\.yml)", readme):
            self.assertTrue((root / ".github/workflows" / workflow).exists())
