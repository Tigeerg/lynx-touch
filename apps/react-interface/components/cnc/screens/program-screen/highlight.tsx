// Colour of one word of a program line: the letter that starts it tells the word type.
export function highlight(line: string) {
  const comment = line.match(/\(.*\)/)?.[0]
  const code = comment ? line.replace(comment, '') : line
  const parts = code.split(/(\s+)/).map((word, i) => {
    const letter = word[0]
    const tone =
      letter === 'N'
        ? 'text-muted-foreground'
        : letter === 'G'
          ? 'text-primary font-bold'
          : letter === 'M'
            ? 'text-destructive font-bold'
            : letter === 'T' || letter === 'S' || letter === 'F'
              ? 'text-success font-semibold'
              : ''
    return (
      <span key={i} className={tone}>
        {word}
      </span>
    )
  })
  return (
    <>
      {parts}
      {comment && <span className="italic text-muted-foreground">{comment}</span>}
    </>
  )
}
