import * as psi from 'psi_'
import { ERROR_INSTANCE_DELETED } from './constants'

export type MatchResult = {
  /**
   * Setup elements equal to at least one decrypted response element: the
   * value `getIntersectionSize` returns for the same setup and response.
   */
  readonly intersectionSize: number
  /** Response elements decrypted, each exactly once. */
  readonly decryptedCount: number
  /**
   * Identifier-revealing matches only: the response indices and the setup
   * indices of each equal pair, in response order. The pairs are those
   * `getAssociationTable` returns.
   */
  readonly associationTable?: readonly [Uint32Array, Uint32Array]
}

export type Match = {
  readonly delete: () => void
  readonly addSetupBytes: (bytes: Uint8Array) => void
  readonly sealSetup: () => void
  readonly matchResponsePiece: (bytes: Uint8Array) => void
  readonly finish: () => MatchResult
}

/**
 * @implements Match
 */
export const MatchConstructor = (instance: psi.Match): Match => {
  let _instance: psi.Match | null = instance

  const live = (): psi.Match => {
    if (!_instance) {
      throw new Error(ERROR_INSTANCE_DELETED)
    }
    return _instance
  }

  const check = ({ Status }: psi.Result): void => {
    if (Status) {
      throw new Error(Status.Message)
    }
  }

  /**
   * @interface Match
   */
  return {
    /**
     * Delete the underlying instance, freeing the setup it holds.
     *
     * @function
     * @name Match#delete
     */
    delete(): void {
      live().delete()
      _instance = null
    },

    /**
     * Appends the next bytes of the serialized server setup. The setup may be
     * cut anywhere; pass its pieces in order.
     *
     * @function
     * @name Match#addSetupBytes
     * @param {Uint8Array} bytes The next bytes of the serialized ServerSetup
     */
    addSetupBytes(bytes: Uint8Array): void {
      check(live().AddSetupBytes(bytes))
    },

    /**
     * Ends the setup. Throws if the bytes passed are not one whole Raw setup
     * whose elements are strictly ascending.
     *
     * @function
     * @name Match#sealSetup
     */
    sealSetup(): void {
      check(live().SealSetup())
    },

    /**
     * Decrypts and matches the next bytes of the serialized server response.
     * The response may be cut anywhere; pass its pieces in order.
     *
     * @function
     * @name Match#matchResponsePiece
     * @param {Uint8Array} bytes The next bytes of the serialized Response
     */
    matchResponsePiece(bytes: Uint8Array): void {
      check(live().MatchResponsePiece(bytes))
    },

    /**
     * Ends the response and returns the result.
     *
     * @function
     * @name Match#finish
     * @returns {MatchResult} The match's result
     */
    finish(): MatchResult {
      const result = live().Finish()
      check(result)
      const {
        IntersectionSize,
        DecryptedCount,
        ResponseIndices,
        SetupIndices
      } = result.Value
      return {
        intersectionSize: IntersectionSize,
        decryptedCount: DecryptedCount,
        ...(ResponseIndices &&
          SetupIndices && {
            associationTable: [ResponseIndices, SetupIndices] as const
          })
      }
    }
  }
}
