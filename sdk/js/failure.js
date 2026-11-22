export class Failure extends Error {
  constructor(kind, message, { cell } = {}) {
    super(message);
    this.name = "Failure";
    this.kind = kind;
    this.cell = cell;
  }
}
