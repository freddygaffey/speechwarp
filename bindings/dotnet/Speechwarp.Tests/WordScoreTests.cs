using Speechwarp;

namespace Speechwarp.Tests;

[TestClass]
public class WordScoreTests
{
    [TestMethod]
    public void ExactMatchIgnoresCaseAndPunctuation()
    {
        Assert.AreEqual(new WordScore(1, 6, 0, 0, 0), WordScore.Of("The cat sat on the mat.", "\"the CAT, sat on the mat!\""));
    }

    [TestMethod]
    public void CountsMissedWrongAndExtra()
    {
        Assert.AreEqual(new WordScore(0.5, 2, 0, 2, 1), WordScore.Of("one two three four", "one too tree four five"));
        Assert.AreEqual(new WordScore(4.0 / 6, 4, 2, 0, 0), WordScore.Of("the cat sat on the mat", "the cat on mat"));
        Assert.AreEqual(new WordScore(0.5, 1, 1, 0, 1), WordScore.Of("a b", "b a"));
    }

    [TestMethod]
    public void ApostrophesAccentsAndEmptyText()
    {
        Assert.AreEqual(1.0, WordScore.Of("Don't stop", "don’t stop").Share);
        Assert.AreEqual(1.0, WordScore.Of("Café au lait", "CAFÉ au lait").Share);
        // Decomposed e plus acute accent matches the composed letter after normalisation.
        Assert.AreEqual(1.0, WordScore.Of("café", "café").Share);
        Assert.AreEqual(new WordScore(1, 0, 0, 0, 0), WordScore.Of("", ""));
        Assert.AreEqual(new WordScore(0, 0, 0, 0, 1), WordScore.Of("", "hello"));
        Assert.AreEqual(new WordScore(0, 0, 2, 0, 0), WordScore.Of("hello world", ""));
        Assert.ThrowsExactly<ArgumentNullException>(() => WordScore.Of(null!, "x"));
    }
}
